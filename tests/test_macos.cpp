#include "Check.h"
#include "platform/ImageIo.h"
#include "platform/Paths.h"
#include "platform/Hotkeys.h"
#include <filesystem>
#include <unistd.h>
using namespace daveshot;
int main(int argc, char** argv) {
    std::string error;
    CHECK(paths::Init(argc ? argv[0] : nullptr, error));
    const auto folder = std::filesystem::temp_directory_path() / ("daveshot-test-" + std::to_string(getpid()));
    CHECK(paths::EnsureFolder(folder.string(), error));
    // Asymmetric rows and channels detect vertical flips and RGBA/BGRA swaps.
    Image original{2, 2, {255,0,0,255, 0,255,0,255, 0,0,255,255, 128,64,32,255}};
    auto png = (folder / "色 image.png").string();
    CHECK(imageio::Save(png, original, imageio::Format::Png, 90, error));
    Image loaded;
    CHECK(imageio::Load(png, loaded, error));
    CHECK(loaded.width == 2 && loaded.height == 2);
    CHECK(loaded.pixels == original.pixels);
    auto jpg = (folder / "image.jpg").string();
    CHECK(imageio::Save(jpg, original, imageio::Format::Jpeg, 90, error));
    CHECK(imageio::Load(jpg, loaded, error));
    CHECK(loaded.Valid() && loaded.width == 2 && loaded.height == 2);
    CHECK(!imageio::Load((folder / "missing.png").string(), loaded, error));
    CHECK(!imageio::Save(png, {}, imageio::Format::Png, 90, error));
    CHECK(hotkeys::IsParseable("Cmd+Shift+S"));
    CHECK(hotkeys::IsParseable("Ctrl+Alt+S"));
    CHECK(!hotkeys::IsParseable("Cmd+Bogus+S"));
    CHECK(!hotkeys::IsParseable("Cmd+Shift+"));
    CHECK(!hotkeys::IsParseable(""));
    std::filesystem::remove_all(folder);
    return testing::Summary("macos");
}
