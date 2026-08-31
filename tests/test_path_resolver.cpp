#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "core/config/config.h"
#include "core/paths/path_resolver.h"

using namespace dw;

// Cross-platform absolute path for testing
#ifdef _WIN32
static const Path kAbsTestPath("C:/some/absolute/path/file.stl");
static const Path kAbsOtherPath("C:/completely/different/location/file.stl");
#else
static const Path kAbsTestPath("/some/absolute/path/file.stl");
static const Path kAbsOtherPath("/completely/different/location/file.stl");
#endif

namespace {

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        static std::atomic<unsigned long long> sequence{0};
        m_path = std::filesystem::temp_directory_path() /
                 ("dw-path-resolver-" + std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const Path& path() const { return m_path; }

  private:
    Path m_path;
};

#ifdef __linux__
class ScopedXdgRuntimeDir {
  public:
    explicit ScopedXdgRuntimeDir(const Path& path) {
        if (const char* current = std::getenv("XDG_RUNTIME_DIR"))
            m_previous = current;
        m_valid = setenv("XDG_RUNTIME_DIR", path.c_str(), 1) == 0;
    }

    ~ScopedXdgRuntimeDir() {
        if (!m_valid)
            return;
        if (m_previous)
            (void)setenv("XDG_RUNTIME_DIR", m_previous->c_str(), 1);
        else
            (void)unsetenv("XDG_RUNTIME_DIR");
    }

    bool valid() const { return m_valid; }

  private:
    std::optional<std::string> m_previous;
    bool m_valid = false;
};
#endif

} // namespace

TEST(PathResolver, AbsolutePathPassesThrough) {
    Path result = PathResolver::resolve(kAbsTestPath, PathCategory::Support);
    EXPECT_EQ(result, kAbsTestPath);
}

#ifdef __linux__
TEST(PathResolver, StaleKioFusePathRebasesToCurrentGenericMount) {
    TemporaryDirectory temporary;
    ScopedXdgRuntimeDir runtimeDir(temporary.path());
    ASSERT_TRUE(runtimeDir.valid());
    const Path stale = temporary.path() /
                       "kio-fuse-Stale123/smb/workshop-nas.local/Models/Signs/river.stl";
    const Path current = temporary.path() /
                         "kio-fuse-Live456/smb/workshop-nas.local/Models/Signs/river.stl";
    std::filesystem::create_directories(current.parent_path());
    std::ofstream(current) << "mesh";

    EXPECT_EQ(PathResolver::resolve(stale, PathCategory::Support), current);
}

TEST(PathResolver, MakeStorablePersistsDurableNetworkUrl) {
    TemporaryDirectory temporary;
    ScopedXdgRuntimeDir runtimeDir(temporary.path());
    ASSERT_TRUE(runtimeDir.valid());
    const Path bridge = temporary.path() /
                        "kio-fuse-Ab12Cd/smb/workshop-nas.local/Model Share/River Sign.stl";

    EXPECT_EQ(PathResolver::makeStorable(bridge, PathCategory::Support),
              Path("smb://workshop-nas.local/Model%20Share/River%20Sign.stl"));
}

TEST(PathResolver, DurableLocationUsesNetworkUrlWithoutMounting) {
    TemporaryDirectory temporary;
    ScopedXdgRuntimeDir runtimeDir(temporary.path());
    ASSERT_TRUE(runtimeDir.valid());
    const Path bridge = temporary.path() /
                        "kio-fuse-Ab12Cd/smb/workshop-nas.local/Model Share/River Sign.stl";

    EXPECT_EQ(PathResolver::durableLocation(bridge, PathCategory::Support),
              Path("smb://workshop-nas.local/Model%20Share/River%20Sign.stl"));
}

TEST(PathResolver, FileManagerParentUsesReconnectableNetworkUrl) {
    TemporaryDirectory temporary;
    ScopedXdgRuntimeDir runtimeDir(temporary.path());
    ASSERT_TRUE(runtimeDir.valid());
    const Path stale = temporary.path() /
                       "kio-fuse-Old123/smb/workshop-nas.local/Model Share/Signs/River Sign.stl";

    EXPECT_EQ(PathResolver::fileManagerParent(stale, PathCategory::Support),
              Path("smb://workshop-nas.local/Model%20Share/Signs"));
}

#endif

TEST(PathResolver, UnsafeNetworkCandidatesFailClosed) {
    for (const Path& unsafe : {
             Path("smb://alice:secret@nas.local/share/model.stl"),
             Path("smb://nas.local/share/model.stl?token=secret"),
             Path("javascript://alert/model.stl"),
         }) {
        EXPECT_TRUE(PathResolver::resolve(unsafe, PathCategory::Support).empty());
        EXPECT_TRUE(PathResolver::makeStorable(unsafe, PathCategory::Support).empty());
        EXPECT_TRUE(PathResolver::durableLocation(unsafe, PathCategory::Support).empty());
        EXPECT_TRUE(PathResolver::fileManagerParent(unsafe, PathCategory::Support).empty());
    }
}

TEST(PathResolver, RelativePathGetsResolved) {
    Path rel("ab/cd/abcd1234.stl");
    Path result = PathResolver::resolve(rel, PathCategory::Support);
    // Should be category root + relative
    EXPECT_TRUE(result.is_absolute());
    EXPECT_TRUE(result.string().find("abcd1234.stl") != std::string::npos);
}

TEST(PathResolver, ModelFileManagerHelpersHonorSupportCategory) {
    const Path relative("signs/river.stl");
    const Path expected = PathResolver::categoryRoot(PathCategory::Support) / relative;

    EXPECT_EQ(PathResolver::durableLocation(relative, PathCategory::Support), expected);
    EXPECT_EQ(PathResolver::fileManagerParent(relative, PathCategory::Support),
              expected.parent_path());
}

TEST(PathResolver, EmptyPathReturnsEmpty) {
    Path empty;
    EXPECT_TRUE(PathResolver::resolve(empty, PathCategory::Models).empty());
    EXPECT_TRUE(PathResolver::makeStorable(empty, PathCategory::Models).empty());
    EXPECT_TRUE(PathResolver::durableLocation(empty, PathCategory::Models).empty());
    EXPECT_TRUE(PathResolver::fileManagerParent(empty, PathCategory::Models).empty());
}

TEST(PathResolver, MakeStorableInsideRoot) {
    Path root = PathResolver::categoryRoot(PathCategory::Support);
    Path absFile = root / "ab" / "cd" / "test.stl";
    Path stored = PathResolver::makeStorable(absFile, PathCategory::Support);
    EXPECT_TRUE(stored.is_relative());
    EXPECT_EQ(stored, Path("ab/cd/test.stl"));
}

TEST(PathResolver, MakeStorableOutsideRoot) {
    Path stored = PathResolver::makeStorable(kAbsOtherPath, PathCategory::Support);
    EXPECT_TRUE(stored.is_absolute());
    EXPECT_EQ(stored, kAbsOtherPath);
}

TEST(PathResolver, RoundTrip) {
    Path root = PathResolver::categoryRoot(PathCategory::GCode);
    Path absFile = root / "myfile.nc";

    Path stored = PathResolver::makeStorable(absFile, PathCategory::GCode);
    EXPECT_TRUE(stored.is_relative());

    Path resolved = PathResolver::resolve(stored, PathCategory::GCode);
    EXPECT_EQ(resolved, absFile);
}

TEST(PathResolver, CategoryRootsAreAbsolute) {
    EXPECT_TRUE(PathResolver::categoryRoot(PathCategory::Models).is_absolute());
    EXPECT_TRUE(PathResolver::categoryRoot(PathCategory::Projects).is_absolute());
    EXPECT_TRUE(PathResolver::categoryRoot(PathCategory::Materials).is_absolute());
    EXPECT_TRUE(PathResolver::categoryRoot(PathCategory::GCode).is_absolute());
    EXPECT_TRUE(PathResolver::categoryRoot(PathCategory::Support).is_absolute());
}
