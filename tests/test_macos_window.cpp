// Real SDL/Metal rendering check. Run manually on a logged-in Mac; this does
// not need screen-recording access, because it reads our own render target.
#include "Check.h"
#include "app/AppState.h"
#include "platform/Host.h"
#include "platform/Paths.h"
#include "platform/SingleInstance.h"
#include <Carbon/Carbon.h>
#include <unistd.h>
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Textures.h"
#include "ui/UiRoot.h"
#include <SDL3/SDL.h>
#include "imgui_internal.h"

int main(int argc, char** argv) {
    using namespace daveshot;
    std::string error;
    if (!paths::Init(argv[0], error)) return 1;
    Host host;
    if (!host.Startup("daveshot rendering check", 1280, 800, true, error)) {
        std::printf("%s\n", error.c_str()); return 1;
    }
    // Never load or write the user's saved window layout.
    ImGui::GetIO().IniFilename = nullptr;
    CHECK(fonts::Load(17.0f, error));
    theme::Apply(0, host.DisplayScale());
    AppState state;
    state.layoutPending = true;
    ui::TextureCache textures;
    textures.SetHost(&host);
    for (int frame = 0; frame < 8; ++frame) {
        host.PumpEvents();
        host.BeginFrame();
        ui::Draw(state, textures);
        host.EndFrame(0.1f, 0.1f, 0.1f);
        SDL_Delay(20);
    }
    const float content = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(host.Window()));
    CHECK_NEAR(host.DisplayScale(), content);
    CHECK_NEAR(ImGui::GetFontSize(), 17.0f * content);
    for (const char* name : {ui::kWindowCapture, ui::kWindowPreview, ui::kWindowSettings, ui::kWindowHistory}) {
        const ImGuiWindow* window = ImGui::FindWindowByName(name);
        CHECK(window && window->DockIsActive);
        CHECK(window && window->Size.x >= 200.0f);
    }
    std::printf("Display scale %.2f, pixel density %.2f, logical UI scale %.2f, font %.2f\n",
        SDL_GetWindowDisplayScale(host.Window()), SDL_GetWindowPixelDensity(host.Window()),
        host.DisplayScale(), ImGui::GetFontSize());
    // Render into a texture for a deterministic screenshot before presentation.
    int width, height;
    SDL_GetRenderOutputSize(host.Renderer(), &width, &height);
    SDL_Texture* target = SDL_CreateTexture(host.Renderer(), SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    CHECK(target != nullptr);
    if (target) {
        SDL_SetRenderTarget(host.Renderer(), target);
        host.BeginFrame();
        ui::Draw(state, textures);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(display.x - 16, display.y - 16), display, IM_COL32(0, 255, 0, 255));
        host.EndFrame(0.1f, 0.1f, 0.1f);
        SDL_Surface* pixels = SDL_RenderReadPixels(host.Renderer(), nullptr);
        CHECK(pixels != nullptr);
        if (pixels) {
            Uint8 r, g, b, a;
            CHECK(SDL_ReadSurfacePixel(pixels, width - 8, height - 8, &r, &g, &b, &a));
            CHECK(r == 0 && g == 255 && b == 0);
            if (argc > 1) CHECK(SDL_SaveBMP(pixels, argv[1]));
            SDL_DestroySurface(pixels);
        }
        SDL_SetRenderTarget(host.Renderer(), nullptr);
        SDL_DestroyTexture(target);
    }
    // Exercise the actual Apple Event sent by a Dock click, repeatedly while
    // hidden. This must work even if the app never lost activation.
    (void)instance::TakeWakeup();
    for (int attempt = 0; attempt < 2; ++attempt) {
        host.Hide();
        CHECK(host.Hidden());
        const pid_t pid = getpid();
        AEAddressDesc address = {typeNull, nullptr};
        AppleEvent event = {typeNull, nullptr};
        CHECK(AECreateDesc(typeKernelProcessID, &pid, sizeof(pid), &address) == noErr);
        CHECK(AECreateAppleEvent(kCoreEventClass, kAEReopenApplication, &address,
                                kAutoGenerateReturnID, kAnyTransactionID, &event) == noErr);
        CHECK(AESendMessage(&event, nullptr, kAENoReply, kAEDefaultTimeout) == noErr);
        AEDisposeDesc(&event);
        AEDisposeDesc(&address);
        bool reopened = false;
        for (int frame = 0; frame < 100 && !reopened; ++frame) {
            host.PumpEvents();
            reopened = instance::TakeWakeup();
            SDL_Delay(10);
        }
        CHECK(reopened);
        CHECK(!instance::TakeWakeup());
        if (reopened) host.Show();
        CHECK(!host.Hidden());
    }
    return testing::Summary("macos_window");
}
