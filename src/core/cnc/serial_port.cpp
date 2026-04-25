#include "serial_port.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "../utils/log.h"

namespace dw {

#ifndef _WIN32
namespace {

speed_t toBaudConstant(int baudRate) {
    switch (baudRate) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return B115200;
    }
}

} // namespace
#endif // !_WIN32

// ── POSIX implementation ──────────────────────────────────────────────

#ifndef _WIN32

SerialPort::~SerialPort() {
    close();
}

SerialPort::SerialPort(SerialPort&& other) noexcept
    : m_fd(other.m_fd), m_device(std::move(other.m_device)),
      m_readBuffer(std::move(other.m_readBuffer)),
      m_connectionState(other.m_connectionState) {
    other.m_fd = -1;
    other.m_connectionState = ConnectionState::Closed;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        m_fd = other.m_fd;
        m_device = std::move(other.m_device);
        m_readBuffer = std::move(other.m_readBuffer);
        m_connectionState = other.m_connectionState;
        other.m_fd = -1;
        other.m_connectionState = ConnectionState::Closed;
    }
    return *this;
}

bool SerialPort::open(const std::string& device, int baudRate) {
    close();

    m_fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        log::errorf("Serial", "Failed to open %s: %s", device.c_str(), strerror(errno));
        return false;
    }

    // Configure 8N1, no flow control
    struct termios tty {};
    if (tcgetattr(m_fd, &tty) != 0) {
        log::errorf("Serial", "tcgetattr failed: %s", strerror(errno));
        close();
        return false;
    }

    speed_t baud = toBaudConstant(baudRate);
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    // Raw mode
    cfmakeraw(&tty);

    // 8N1
    tty.c_cflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(CSIZE | PARENB | CSTOPB));
    tty.c_cflag |= CS8;

    // Enable receiver, ignore modem status lines, suppress DTR on close
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(HUPCL)); // Prevent DTR toggle on close

    // No hardware flow control
    tty.c_cflag &= ~CRTSCTS;

    // No software flow control
    tty.c_iflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(IXON | IXOFF | IXANY));

    // Non-blocking reads (poll-based)
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        log::errorf("Serial", "tcsetattr failed: %s", strerror(errno));
        close();
        return false;
    }

    // Flush any stale data
    tcflush(m_fd, TCIOFLUSH);

    // Explicitly lower DTR to prevent Arduino auto-reset on connect
    int modemBits = TIOCM_DTR;
    ioctl(m_fd, TIOCMBIC, &modemBits);

    m_device = device;
    m_readBuffer.clear();
    m_connectionState = ConnectionState::Connected;
    log::infof("Serial", "Opened %s at %d baud", device.c_str(), baudRate);
    return true;
}

void SerialPort::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
        log::infof("Serial", "Closed %s", m_device.c_str());
    }
    m_readBuffer.clear();
    m_connectionState = ConnectionState::Closed;
}

bool SerialPort::write(const std::string& data) {
    if (m_fd < 0)
        return false;

    size_t totalWritten = 0;
    while (totalWritten < data.size()) {
        ssize_t written = ::write(m_fd, data.c_str() + totalWritten,
                                  data.size() - totalWritten);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EIO || errno == ENXIO || errno == ENODEV)
                m_connectionState = ConnectionState::Disconnected;
            log::errorf("Serial", "Write failed: %s", strerror(errno));
            return false;
        }
        totalWritten += static_cast<size_t>(written);
    }
    return true;
}

bool SerialPort::writeByte(u8 byte) {
    if (m_fd < 0)
        return false;

    ssize_t written = ::write(m_fd, &byte, 1);
    if (written != 1) {
        if (errno == EIO || errno == ENXIO || errno == ENODEV)
            m_connectionState = ConnectionState::Disconnected;
        return false;
    }
    return true;
}

std::optional<std::string> SerialPort::readLine(int timeoutMs) {
    if (m_fd < 0)
        return std::nullopt;

    // Check if we already have a complete line in the buffer
    auto nlPos = m_readBuffer.find('\n');
    if (nlPos != std::string::npos) {
        std::string line = m_readBuffer.substr(0, nlPos);
        m_readBuffer.erase(0, nlPos + 1);
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return line;
    }

    // Poll for data with monotonic clock for accurate timeout tracking
    struct pollfd pfd {};
    pfd.fd = m_fd;
    pfd.events = POLLIN;

    auto startTime = std::chrono::steady_clock::now();
    int remaining = timeoutMs;

    while (remaining > 0) {
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) {
                // Recalculate remaining time after interrupt
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                remaining = timeoutMs - static_cast<int>(elapsed);
                continue;
            }
            m_connectionState = ConnectionState::Error;
            return std::nullopt;
        }
        if (ret == 0)
            return std::nullopt; // Timeout (not an error)

        // Check for device disconnect/error BEFORE checking for data
        if (pfd.revents & (POLLHUP | POLLERR)) {
            log::error("Serial", "Device disconnected (POLLHUP/POLLERR)");
            m_connectionState = ConnectionState::Disconnected;
            return std::nullopt;
        }

        if (pfd.revents & POLLIN) {
            char buf[256];
            ssize_t n = ::read(m_fd, buf, sizeof(buf));
            if (n <= 0) {
                // Zero-length or error read on serial typically means device gone
                m_connectionState = ConnectionState::Disconnected;
                return std::nullopt;
            }

            m_readBuffer.append(buf, static_cast<size_t>(n));

            nlPos = m_readBuffer.find('\n');
            if (nlPos != std::string::npos) {
                std::string line = m_readBuffer.substr(0, nlPos);
                m_readBuffer.erase(0, nlPos + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return line;
            }
        }

        // Recalculate remaining time using monotonic clock
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        remaining = timeoutMs - static_cast<int>(elapsed);
    }

    return std::nullopt;
}

void SerialPort::drain() {
    if (m_fd < 0)
        return;
    tcdrain(m_fd);
    tcflush(m_fd, TCIFLUSH);
    m_readBuffer.clear();
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> ports;

    namespace fs = std::filesystem;
    const std::string devPath = "/dev";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(devPath, ec)) {
        if (!entry.is_character_file(ec))
            continue;
        std::string name = entry.path().filename().string();
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0) {
            ports.push_back(entry.path().string());
        }
    }

    std::sort(ports.begin(), ports.end());
    return ports;
}

namespace {

// Read a single-line sysfs attribute, trimming trailing newline
std::string readSysfsAttr(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    std::getline(f, line);
    return line;
}

// Check if product/manufacturer strings suggest a CNC controller
bool isCncLikely(const std::string& product, const std::string& manufacturer,
                 const std::string& driver) {
    // Known CNC controller product strings (case-insensitive substring match)
    static const char* cncKeywords[] = {
        "grbl", "GRBL", "Grbl",
        "arduino", "Arduino", "ARDUINO",
        "CNC", "cnc",
        "FluidNC", "fluidnc",
        "Smoothie", "smoothie",
        "TinyG", "tinyg",
        "Buildbotics", "buildbotics",
        "xPro", "xpro",
        "Mega 2560",    // Common GRBL board
        "Uno",          // Arduino Uno (common GRBL)
        "Nano",         // Arduino Nano
    };

    auto contains = [](const std::string& haystack, const char* needle) {
        return haystack.find(needle) != std::string::npos;
    };

    for (const char* kw : cncKeywords) {
        if (contains(product, kw) || contains(manufacturer, kw))
            return true;
    }

    // ttyACM devices with cdc_acm driver are more likely to be Arduino/CNC
    // than ttyUSB devices with generic ch341/cp210x/ftdi drivers
    if (driver == "cdc_acm")
        return true;

    return false;
}

} // anonymous namespace

std::vector<SerialPortInfo> listSerialPortsDetailed() {
    std::vector<SerialPortInfo> result;
    namespace fs = std::filesystem;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/dev", ec)) {
        if (!entry.is_character_file(ec))
            continue;
        std::string name = entry.path().filename().string();
        if (name.find("ttyUSB") != 0 && name.find("ttyACM") != 0)
            continue;

        SerialPortInfo info;
        info.device = entry.path().string();

        // Read USB metadata from sysfs
        fs::path sysPath = fs::path("/sys/class/tty") / name / "device";
        if (fs::exists(sysPath, ec)) {
            // Driver name from symlink target
            fs::path driverLink = sysPath / "driver";
            if (fs::is_symlink(driverLink, ec)) {
                info.driver = fs::read_symlink(driverLink, ec).filename().string();
            }

            // Walk up to USB device level for product/manufacturer
            // ttyUSB: device/../ is the USB interface, ../../ is the USB device
            // ttyACM: device/../ is the USB interface, ../../ is the USB device
            for (fs::path usbDir = fs::canonical(sysPath, ec);
                 !usbDir.empty() && usbDir != "/sys";
                 usbDir = usbDir.parent_path()) {
                fs::path prodFile = usbDir / "product";
                if (fs::exists(prodFile, ec)) {
                    info.product = readSysfsAttr(prodFile);
                    info.manufacturer = readSysfsAttr(usbDir / "manufacturer");
                    break;
                }
            }
        }

        info.likelyCnc = isCncLikely(info.product, info.manufacturer, info.driver);
        result.push_back(std::move(info));
    }

    std::sort(result.begin(), result.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) {
                  // Sort CNC-likely devices first, then by device name
                  if (a.likelyCnc != b.likelyCnc) return a.likelyCnc > b.likelyCnc;
                  return a.device < b.device;
              });

    return result;
}

// ── Windows stubs (serial not yet implemented) ───────────────────────

#else // _WIN32

SerialPort::~SerialPort() { close(); }
SerialPort::SerialPort(SerialPort&& other) noexcept
    : m_fd(other.m_fd), m_device(std::move(other.m_device)),
      m_readBuffer(std::move(other.m_readBuffer)) { other.m_fd = -1; }
SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) { close(); m_fd = other.m_fd; m_device = std::move(other.m_device);
        m_readBuffer = std::move(other.m_readBuffer); other.m_fd = -1; }
    return *this;
}

bool SerialPort::open(const std::string& device, int /*baudRate*/) {
    log::errorf("Serial", "Serial port not yet implemented on Windows: %s", device.c_str());
    return false;
}
void SerialPort::close() { m_fd = -1; m_readBuffer.clear(); }
bool SerialPort::write(const std::string& /*data*/) { return false; }
bool SerialPort::writeByte(u8 /*byte*/) { return false; }
std::optional<std::string> SerialPort::readLine(int /*timeoutMs*/) { return std::nullopt; }
void SerialPort::drain() { m_readBuffer.clear(); }

std::vector<std::string> listSerialPorts() {
    // TODO: enumerate COM ports via SetupAPI/WMI
    return {};
}

std::vector<SerialPortInfo> listSerialPortsDetailed() {
    return {};
}

#endif // _WIN32

} // namespace dw
