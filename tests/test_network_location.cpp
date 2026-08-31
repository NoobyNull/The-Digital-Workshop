#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "core/paths/network_location.h"

using namespace dw;

namespace {

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        static std::atomic<unsigned long long> sequence{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("dw-network-location-" + std::to_string(timestamp) + "-" +
                std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(root);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    Path makeKioFile(const std::string& mount,
                     const std::string& location,
                     std::string_view content = "data") const {
        const Path result = root / mount / location;
        std::filesystem::create_directories(result.parent_path());
        std::ofstream(result) << content;
        return result;
    }

    Path root;
};

const Path kRuntimeRoot("/run/user/1000");

} // namespace

// KIO-FUSE runtime paths are a POSIX-only concept; their '?' segments are not
// even representable as Windows paths.
#ifndef _WIN32
TEST(NetworkLocation, BuildsGenericDurableUrlAndEncodesRemoteSegments) {
    const Path stale(
        "/run/user/1000/kio-fuse-vOVnSN/smb/workshop-nas.local/Model Share/A&B/#plans?/100%.stl");

    auto url = network_location::durableUrl(stale, kRuntimeRoot);
    ASSERT_TRUE(url);
    EXPECT_EQ(*url, "smb://workshop-nas.local/Model%20Share/A%26B/%23plans%3F/100%25.stl");
}
#endif

TEST(NetworkLocation, AcceptsAllowlistedSchemesUsernamePortsAndIpv6) {
    const std::vector<std::string> urls = {
        "smb://nas.local/share/file.stl",
        "sftp://alice@example.test:2222/home/file.nc",
        "fish://alice@example.test/home/file.nc",
        "ftp://example.test:21/files/file.nc",
        "ftps://example.test/files/file.nc",
        "nfs://server.test/export/file.nc",
        "webdav://example.test/share/file.nc",
        "webdavs://[2001:db8::1]:443/share/file.nc",
    };

    for (const auto& url : urls) {
        SCOPED_TRACE(url);
        EXPECT_TRUE(network_location::durableUrl(Path(url)));
    }
    EXPECT_EQ(*network_location::durableUrl(Path("SFTP://Alice@[2001:DB8::1]:022/home/file.nc")),
              "sftp://Alice@[2001:db8::1]:22/home/file.nc");
    EXPECT_EQ(*network_location::durableUrl(Path("smb://nas.local/share/folder/")),
              "smb://nas.local/share/folder");
}

TEST(NetworkLocation, RejectsPasswordsTokensUnsafeSchemesAndMalformedEscapes) {
    const std::vector<std::string> rejected = {
        "smb://alice:secret@nas.local/share/file.stl",
        "smb://alice%3Asecret@nas.local/share/file.stl",
        "smb://nas.local/share/file.stl?token=secret",
        "smb://nas.local/share/file.stl#fragment",
        "javascript://handler/payload",
        "file:///home/user/file.stl",
        "smb://nas.local/share/%ZZ/file.stl",
        "smb://nas.local/share/raw space.stl",
        "smb://nas.local/share/../file.stl",
        "smb://nas.local/share/%2e%2E/file.stl",
        "smb://nas.local/share/%2Fetc/file.stl",
        "smb://2001:db8::1/share/file.stl",
        "smb://nas.local:99999/share/file.stl",
    };

    for (const auto& value : rejected) {
        SCOPED_TRACE(value);
        EXPECT_FALSE(network_location::durableUrl(Path(value)));
        EXPECT_TRUE(network_location::isNetworkLocationCandidate(Path(value)));
    }
}

#ifndef _WIN32
TEST(NetworkLocation, RejectsCredentialBearingAndOutOfRuntimeKioPaths) {
    const Path credential(
        "/run/user/1000/kio-fuse-Ab12Cd/sftp/alice:secret@example.test/home/file.nc");
    EXPECT_FALSE(network_location::durableUrl(credential, kRuntimeRoot));
    EXPECT_TRUE(network_location::isNetworkLocationCandidate(credential, kRuntimeRoot));

    const Path outside("/tmp/kio-fuse-Ab12Cd/smb/nas.local/share/file.stl");
    EXPECT_FALSE(network_location::durableUrl(outside, kRuntimeRoot));
    EXPECT_FALSE(network_location::isNetworkLocationCandidate(outside, kRuntimeRoot));
}
#endif

TEST(NetworkLocation, CandidateCatchesMalformedUrlAndKioLookalikes) {
    EXPECT_TRUE(network_location::isNetworkLocationCandidate(Path("not a scheme://payload")));
#ifndef _WIN32
    EXPECT_TRUE(network_location::isNetworkLocationCandidate(
        Path("/run/user/1000/kio-fuse-/smb/nas.local/share/file.stl"), kRuntimeRoot));
    EXPECT_FALSE(network_location::isNetworkLocationCandidate(Path("/home/user/file.stl")));
#endif
}

TEST(NetworkLocation, ParentLocationIsUriAwareAndFailsClosed) {
    EXPECT_EQ(network_location::parentLocation(
                  Path("smb://nas.local/Model%20Share/Signs/River%20Sign.stl")),
              Path("smb://nas.local/Model%20Share/Signs"));
    EXPECT_EQ(network_location::parentLocation(Path("/home/user/file.stl")), Path("/home/user"));
    EXPECT_TRUE(
        network_location::parentLocation(Path("smb://alice:secret@nas.local/share/file.stl"))
            .empty());
}

TEST(NetworkLocation, ExistingKioPathPassesThroughWithoutMounting) {
    TemporaryDirectory temporary;
    const Path live =
        temporary.makeKioFile("kio-fuse-Live12", "smb/nas.local/share/model.stl", "mesh");
    bool mounted = false;

    const Path resolved = network_location::resolve(
        live,
        [&mounted](const std::string&) -> Result<Path> {
            mounted = true;
            return std::nullopt;
        },
        temporary.root);

    EXPECT_EQ(resolved, live);
    EXPECT_FALSE(mounted);
}

TEST(NetworkLocation, RebaseUsesCurrentSiblingMountBeforeMounter) {
    TemporaryDirectory temporary;
    const Path stale = temporary.root / "kio-fuse-Stale1/smb/nas.local/share/folder/model.stl";
    const Path current =
        temporary.makeKioFile("kio-fuse-Current2", "smb/nas.local/share/folder/model.stl", "mesh");
    bool mounted = false;

    const Path resolved = network_location::resolve(
        stale,
        [&mounted](const std::string&) -> Result<Path> {
            mounted = true;
            return std::nullopt;
        },
        temporary.root);

    EXPECT_EQ(resolved, current);
    EXPECT_FALSE(mounted);
}

TEST(NetworkLocation, ValidatesInjectedMounterOutput) {
    TemporaryDirectory temporary;
    const Path stale = temporary.root / "kio-fuse-Stale3/smb/nas.local/Share Name/folder/model.stl";
    Path mounted;
    std::string mountedUrl;

    const Path resolved = network_location::resolve(
        stale,
        [&](const std::string& url) -> Result<Path> {
            mountedUrl = url;
            mounted = temporary.makeKioFile("kio-fuse-Live99",
                                            "smb/nas.local/Share Name/folder/model.stl",
                                            "mesh");
            return mounted;
        },
        temporary.root);

    EXPECT_EQ(mountedUrl, "smb://nas.local/Share%20Name/folder/model.stl");
    EXPECT_EQ(resolved, mounted);
}

TEST(NetworkLocation, CachesValidatedMountWhileItExists) {
    TemporaryDirectory temporary;
    const Path request = temporary.root / "kio-fuse-Old777/smb/cache.local/share/model.stl";
    int mountCalls = 0;
    const auto mounter = [&](const std::string&) -> Result<Path> {
        ++mountCalls;
        return temporary.makeKioFile(
            "kio-fuse-Live777", "smb/cache.local/share/model.stl", "mesh");
    };

    const Path first = network_location::resolve(request, mounter, temporary.root);
    const Path second = network_location::resolve(request, mounter, temporary.root);

    EXPECT_EQ(first, second);
    EXPECT_EQ(mountCalls, 1);
}

TEST(NetworkLocation, CoalescesConcurrentMountRequests) {
    TemporaryDirectory temporary;
    const Path request =
        temporary.root / "kio-fuse-Old888/smb/concurrent.local/share/model.stl";
    constexpr usize workerCount = 8;
    std::atomic<usize> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> mountCalls{0};
    std::mutex gateMutex;
    std::condition_variable entered;
    std::condition_variable release;
    bool allowMount = false;
    std::once_flag createFile;
    Path mounted;

    const auto mounter = [&](const std::string&) -> Result<Path> {
        ++mountCalls;
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            entered.notify_one();
            release.wait(lock, [&allowMount]() { return allowMount; });
        }
        std::call_once(createFile, [&]() {
            mounted = temporary.makeKioFile(
                "kio-fuse-Live888", "smb/concurrent.local/share/model.stl", "mesh");
        });
        return mounted;
    };

    std::vector<Path> results(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (usize index = 0; index < workerCount; ++index) {
        workers.emplace_back([&, index]() {
            ++ready;
            while (!start.load())
                std::this_thread::yield();
            results[index] = network_location::resolve(request, mounter, temporary.root);
        });
    }
    while (ready.load() != workerCount)
        std::this_thread::yield();
    start = true;

    {
        std::unique_lock<std::mutex> lock(gateMutex);
        entered.wait(lock, [&mountCalls]() { return mountCalls.load() > 0; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard<std::mutex> lock(gateMutex);
        allowMount = true;
    }
    release.notify_all();
    for (auto& worker : workers)
        worker.join();

    EXPECT_EQ(mountCalls.load(), 1);
    for (const auto& result : results)
        EXPECT_EQ(result, mounted);
}

TEST(NetworkLocation, RejectsNonexistentNonKioAndWrongUrlMountReplies) {
    TemporaryDirectory temporary;
    const Path request = temporary.root / "kio-fuse-Old123/smb/nas.local/share/model.stl";
    const Path ordinary = temporary.root / "ordinary/model.stl";
    std::filesystem::create_directories(ordinary.parent_path());
    std::ofstream(ordinary) << "mesh";
    const Path wrong =
        temporary.makeKioFile("kio-fuse-Live1", "smb/other.local/share/model.stl", "mesh");

    EXPECT_EQ(network_location::resolve(
                  request,
                  [](const std::string&) -> Result<Path> { return Path("/does/not/exist"); },
                  temporary.root),
              request);
    EXPECT_EQ(
        network_location::resolve(
            request, [&](const std::string&) -> Result<Path> { return ordinary; }, temporary.root),
        request);
    EXPECT_EQ(
        network_location::resolve(
            request, [&](const std::string&) -> Result<Path> { return wrong; }, temporary.root),
        request);
}

TEST(NetworkLocation, MissingLocalPathNeverCallsNetworkMounter) {
    const Path local("/definitely/not/a/kio/location/model.stl");
    bool mounted = false;
    const Path resolved = network_location::resolve(local,
                                                    [&mounted](const std::string&) -> Result<Path> {
                                                        mounted = true;
                                                        return std::nullopt;
                                                    });
    EXPECT_EQ(resolved, local);
    EXPECT_FALSE(mounted);
}
