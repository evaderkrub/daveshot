// End-to-end interface tests.
//
// A real ImGui context with no window and no GPU, driven by the Dear ImGui
// Test Engine. The function under test is ui::Draw -- the same one the
// application calls -- so there is no separate, test-only interface to drift
// away from the real one.
//
// The panels never take a capture themselves: they raise flags on AppState
// that the frame loop acts on. That is what makes them testable here, with no
// screen to capture and no window to hide.

#include "app/AppState.h"
#include "app/Capture.h"
#include "platform/Paths.h"
#include "ui/Fonts.h"
#include "ui/Panels.h"
#include "ui/Textures.h"
#include "ui/Theme.h"
#include "ui/UiRoot.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace daveshot;

namespace
{
    AppState            gState;
    ui::TextureCache    gTextures;   // no host: every texture comes back as 0

    Image MakeTestImage(int width, int height)
    {
        Image image;
        image.width  = width;
        image.height = height;
        image.pixels.assign((size_t)width * (size_t)height * 4u, 200);
        return image;
    }

    void GiveOneShot()
    {
        gState.history.Clear();
        gState.selectedShotId = 0;
        AcceptShot(gState, MakeShot(MakeTestImage(320, 200), CaptureMode::Region,
                                    Rect{0, 0, 320, 200}, LocalTimeNow()));
    }

    // A renderer that never renders. Declaring RendererHasTextures and then
    // acknowledging every request is what lets the real fonts load here: the
    // atlas asks for a texture, we say "done", and glyph metrics -- which is
    // what the layout depends on -- come out identical to the application's.
    void HonorTextureRequests()
    {
        static intptr_t nextId = 1;
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        {
            switch (tex->Status)
            {
            case ImTextureStatus_WantCreate:
                tex->SetTexID((ImTextureID)nextId++);
                tex->SetStatus(ImTextureStatus_OK);
                break;
            case ImTextureStatus_WantUpdates:
                tex->SetStatus(ImTextureStatus_OK);
                break;
            case ImTextureStatus_WantDestroy:
                // imgui asserts that a backend clears the identifier it
                // handed out when it reports the texture destroyed.
                tex->SetTexID(ImTextureID_Invalid);
                tex->BackendUserData = nullptr;
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            default:
                break;
            }
        }
    }

    // Mirrors AppLoop's frame, minus the parts that need an OS window. The
    // style rebuild sits before NewFrame for the same reason it does there:
    // ScaleAllSizes is not idempotent and mid-frame metric changes leave the
    // rest of the frame measured against the old numbers.
    void RunOneFrame(ImGuiTestEngine* engine)
    {
        if (StyleNeedsRebuild(gState))
        {
            theme::Apply(theme::IndexByName(gState.settings.theme.c_str()),
                         gState.settings.uiScale);
            MarkStyleApplied(gState);
        }

        ImGui::NewFrame();
        ui::Draw(gState, gTextures);
        ImGui::Render();
        HonorTextureRequests();

        // PostSwap is the engine's "frame presented" signal. With no swap to
        // wait for, it goes immediately after Render().
        ImGuiTestEngine_PostSwap(engine);
    }

    void RegisterTests(ImGuiTestEngine* engine)
    {
        ImGuiTest* t = nullptr;

        t = IM_REGISTER_TEST(engine, "ui", "opens_with_the_capture_panel");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            ctx->SetRef(ui::kWindowCapture);
            IM_CHECK(ctx->WindowInfo("").Window != nullptr);
            IM_CHECK(ctx->ItemExists("CaptureRegion"));
            IM_CHECK(ctx->ItemExists("CaptureFullScreen"));
        };

        t = IM_REGISTER_TEST(engine, "ui", "capture_buttons_raise_a_request");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.pendingRequest = false;
            gState.phase = CapturePhase::Idle;
            ctx->Yield(2);

            ctx->SetRef(ui::kWindowCapture);
            ctx->ItemClick("CaptureRegion");
            ctx->Yield();

            // The panel asks; it never captures. Anything else would put a
            // screen grab inside the draw code.
            IM_CHECK(gState.pendingRequest);
            IM_CHECK(gState.request.mode == CaptureMode::Region);

            gState.pendingRequest = false;
            ctx->ItemClick("CaptureFullScreen");
            ctx->Yield();
            IM_CHECK(gState.pendingRequest);
            IM_CHECK(gState.request.mode == CaptureMode::FullScreen);
            gState.pendingRequest = false;
        };

        t = IM_REGISTER_TEST(engine, "ui", "capture_buttons_lock_while_busy");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            // A second request mid-countdown is never what anyone means, and
            // a disabled button says so better than a silent no-op.
            gState.phase = CapturePhase::Countdown;
            gState.countdownEnd = ImGui::GetTime() + 5.0;
            gState.pendingRequest = false;
            ctx->Yield(2);

            ctx->SetRef(ui::kWindowCapture);
            ImGuiTestItemInfo item = ctx->ItemInfo("CaptureRegion");
            IM_CHECK((item.ItemFlags & ImGuiItemFlags_Disabled) != 0);

            gState.phase = CapturePhase::Idle;
            ctx->Yield(2);
            item = ctx->ItemInfo("CaptureRegion");
            IM_CHECK((item.ItemFlags & ImGuiItemFlags_Disabled) == 0);
        };

        t = IM_REGISTER_TEST(engine, "ui", "delay_is_clamped_by_the_slider");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            ctx->SetRef(ui::kWindowCapture);
            ctx->ItemInputValue("Delay", 5);
            ctx->Yield();
            IM_CHECK_EQ(gState.request.delaySeconds, 5);

            ctx->ItemInputValue("Delay", 0);
            ctx->Yield();
            IM_CHECK_EQ(gState.request.delaySeconds, 0);
        };

        t = IM_REGISTER_TEST(engine, "ui", "preview_is_empty_until_a_capture");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.history.Clear();
            gState.selectedShotId = 0;
            ctx->Yield(2);

            ctx->SetRef(ui::kWindowPreview);
            IM_CHECK(!ctx->ItemExists("CopyShot"));

            GiveOneShot();
            ctx->Yield(2);
            IM_CHECK(ctx->ItemExists("CopyShot"));
            IM_CHECK(ctx->ItemExists("SaveShot"));
        };

        t = IM_REGISTER_TEST(engine, "ui", "save_and_copy_raise_intents");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            GiveOneShot();
            gState.saveRequested = false;
            gState.copyRequested = false;
            ctx->Yield(2);

            ctx->SetRef(ui::kWindowPreview);
            ctx->ItemClick("CopyShot");
            ctx->Yield();
            IM_CHECK(gState.copyRequested);

            gState.copyRequested = false;
            ctx->ItemClick("SaveShot");
            ctx->Yield();
            IM_CHECK(gState.saveRequested);
            gState.saveRequested = false;
        };

        t = IM_REGISTER_TEST(engine, "ui", "reveal_is_locked_until_saved");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            GiveOneShot();
            ctx->Yield(2);

            ctx->SetRef(ui::kWindowPreview);
            ImGuiTestItemInfo item = ctx->ItemInfo("RevealShot");
            IM_CHECK((item.ItemFlags & ImGuiItemFlags_Disabled) != 0);

            // Once it has a path on disk there is somewhere to go.
            CurrentShot(gState)->savedPath = "C:/somewhere/shot.png";
            ctx->Yield(2);
            item = ctx->ItemInfo("RevealShot");
            IM_CHECK((item.ItemFlags & ImGuiItemFlags_Disabled) == 0);
        };

        t = IM_REGISTER_TEST(engine, "ui", "copy_path_needs_a_saved_file");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            GiveOneShot();
            gState.copyPathRequested = false;
            ctx->Yield(2);

            // Copying the path only means something once there is a file: an
            // unsaved capture has no path to hand anyone.
            ctx->SetRef(ui::kWindowPreview);
            ImGuiTestItemInfo item = ctx->ItemInfo("CopyPath");
            IM_CHECK((item.ItemFlags & ImGuiItemFlags_Disabled) != 0);

            CurrentShot(gState)->savedPath = "C:/somewhere/shot.png";
            ctx->Yield(2);
            item = ctx->ItemInfo("CopyPath");
            IM_CHECK((item.ItemFlags & ImGuiItemFlags_Disabled) == 0);

            ctx->ItemClick("CopyPath");
            ctx->Yield();
            IM_CHECK(gState.copyPathRequested);
            gState.copyPathRequested = false;
        };

        t = IM_REGISTER_TEST(engine, "ui", "history_selects_a_shot");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.history.Clear();
            gState.selectedShotId = 0;
            gState.showHistory = true;

            AcceptShot(gState, MakeShot(MakeTestImage(100, 60), CaptureMode::Region,
                                        Rect{}, LocalTimeNow()));
            const unsigned older = gState.history.shots.front().id;
            AcceptShot(gState, MakeShot(MakeTestImage(120, 80), CaptureMode::FullScreen,
                                        Rect{}, LocalTimeNow()));
            const unsigned newest = gState.history.shots.front().id;
            ctx->Yield(3);

            // The newest capture is what the preview shows without asking.
            IM_CHECK(CurrentShot(gState)->id == newest);

            // With no renderer the strip falls back to labelled buttons, which
            // is exactly what makes it clickable here.
            ctx->SetRef(ui::kWindowHistory);
            ctx->ItemClick("**/Region 100x60");
            ctx->Yield(2);
            IM_CHECK(CurrentShot(gState)->id == older);

            ctx->ItemClick("ClearHistory");
            ctx->Yield(2);
            IM_CHECK(gState.history.shots.empty());
            IM_CHECK(CurrentShot(gState) == nullptr);
        };

        t = IM_REGISTER_TEST(engine, "ui", "about_is_modal");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.showAbout = false;
            ctx->Yield(2);

            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("Help/About");
            ctx->Yield(2);

            IM_CHECK(gState.showAbout);

            // The requirement is not merely "a window appeared" -- the About
            // box has to be modal, so assert the flag on the window itself
            // rather than trusting that it looks like a dialog.
            ImGuiWindow* about = ImGui::FindWindowByName("###AboutDaveshot");
            IM_CHECK(about != nullptr);
            IM_CHECK((about->Flags & ImGuiWindowFlags_Modal) != 0);
            IM_CHECK(ImGui::GetTopMostPopupModal() == about);

            ctx->SetRef("###AboutDaveshot");
            ctx->ItemClick("Close");
            ctx->Yield(2);

            IM_CHECK(!gState.showAbout);
            IM_CHECK(ImGui::GetTopMostPopupModal() == nullptr);
        };

        t = IM_REGISTER_TEST(engine, "ui", "errors_surface_as_a_modal");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            // Failures below the interface come back as a string, not an
            // exception. This is where the user actually sees one.
            ReportError(gState, "the clipboard refused the image");
            ctx->Yield(3);

            ImGuiWindow* dialog = ImGui::FindWindowByName("###DaveshotError");
            IM_CHECK(dialog != nullptr);
            IM_CHECK((dialog->Flags & ImGuiWindowFlags_Modal) != 0);

            ctx->SetRef("###DaveshotError");
            ctx->ItemClick("OK");
            ctx->Yield(2);
            IM_CHECK(gState.error.empty());
        };

        t = IM_REGISTER_TEST(engine, "ui", "scale_changes_metrics");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            SetUiScale(gState, 1.0f);
            gState.showSettings = true;
            ctx->Yield(3);
            const float basePadding = ImGui::GetStyle().FramePadding.x;
            const float baseFont    = ImGui::GetStyle().FontScaleMain;

            ctx->SetRef(ui::kWindowSettings);
            ctx->ItemInputValue("UI scale", 2.0f);
            ctx->Yield(2);

            IM_CHECK_EQ(gState.settings.uiScale, 2.0f);
            // Both halves of "scale the gui" have to move: the metrics and the
            // text. Checking only the slider value would pass even if the
            // rebuild never ran.
            IM_CHECK_GT(ImGui::GetStyle().FramePadding.x, basePadding);
            IM_CHECK_GT(ImGui::GetStyle().FontScaleMain, baseFont);

            // Re-applying the same scale must not compound: ScaleAllSizes
            // multiplies, so a metric that grows on every apply is the bug
            // this check exists to catch.
            const float scaledPadding = ImGui::GetStyle().FramePadding.x;
            theme::Apply(theme::CurrentIndex(), gState.settings.uiScale);
            ctx->Yield(2);
            IM_CHECK_EQ(ImGui::GetStyle().FramePadding.x, scaledPadding);

            ctx->ItemInputValue("UI scale", 1.0f);
            ctx->Yield(2);
            IM_CHECK_EQ(gState.settings.uiScale, 1.0f);
        };

        t = IM_REGISTER_TEST(engine, "ui", "theme_can_be_changed");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            SetTheme(gState, "Slate Dark");
            gState.showSettings = true;
            ctx->Yield(3);

            ctx->SetRef(ui::kWindowSettings);
            ctx->ItemClick("Theme");
            ctx->Yield(2);
            ctx->ItemClick("//$FOCUSED/Nord");
            ctx->Yield(2);

            IM_CHECK_STR_EQ(gState.settings.theme.c_str(), "Nord");
            IM_CHECK_STR_EQ(theme::At(theme::CurrentIndex()).name, "Nord");

            SetTheme(gState, "Slate Dark");
            ctx->Yield(2);
        };

        t = IM_REGISTER_TEST(engine, "ui", "hotkey_change_asks_for_reregistration");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.settings.hotkeyEnabled = true;
            gState.settings.hotkeyRegion  = "Ctrl+Shift+S";
            MarkHotkeysApplied(gState);
            gState.showSettings = true;
            ctx->Yield(3);

            ctx->SetRef(ui::kWindowSettings);
            ctx->ItemClick("Region");
            ctx->Yield(2);
            ctx->ItemClick("//$FOCUSED/Ctrl+Shift+A");
            ctx->Yield(2);

            IM_CHECK_STR_EQ(gState.settings.hotkeyRegion.c_str(), "Ctrl+Shift+A");
            IM_CHECK(HotkeysNeedRegistering(gState));
            MarkHotkeysApplied(gState);
        };

        t = IM_REGISTER_TEST(engine, "ui", "print_screen_can_be_taken_over");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            // The loop asks the OS and leaves the answer here; the panel only
            // reads it. That is what lets both halves of the offer be tested
            // on a machine where the desktop holds nothing.
            gState.printKey.state = printkey::State::Desktop;
            gState.printKey.hint  = "The desktop answers Print Screen itself.";
            gState.takePrintKeyRequested = false;
            gState.givePrintKeyRequested = false;
            gState.showSettings = true;
            ctx->Yield(3);

            ctx->SetRef(ui::kWindowSettings);
            IM_CHECK(!ctx->ItemExists("Give Print Screen back"));
            ctx->ItemClick("Take over Print Screen");
            ctx->Yield(2);
            IM_CHECK(gState.takePrintKeyRequested);

            gState.printKey.state = printkey::State::Ours;
            gState.printKey.hint  = "Print Screen is daveshot's.";
            // The engine reports an item as present for two frames after it
            // stops being drawn, so a "no longer there" check has to outlast
            // that; anything less passes on a stale answer.
            ctx->Yield(4);
            IM_CHECK(!ctx->ItemExists("Take over Print Screen"));
            ctx->ItemClick("Give Print Screen back");
            ctx->Yield(2);
            IM_CHECK(gState.givePrintKeyRequested);

            // Unknown is the answer on a desktop whose binding we cannot
            // read, and on a machine where Print Screen is not a hotkey at
            // all: neither offer belongs there.
            gState.printKey = printkey::Status{};
            gState.takePrintKeyRequested = false;
            gState.givePrintKeyRequested = false;
            ctx->Yield(4);
            IM_CHECK(!ctx->ItemExists("Take over Print Screen"));
            IM_CHECK(!ctx->ItemExists("Give Print Screen back"));
        };

        t = IM_REGISTER_TEST(engine, "ui", "format_switch_reveals_quality");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.settings.format = ImageFormat::Png;
            gState.showSettings = true;
            ctx->Yield(3);

            ctx->SetRef(ui::kWindowSettings);
            IM_CHECK(!ctx->ItemExists("Quality"));

            ctx->ItemClick("Format");
            ctx->Yield(2);
            ctx->ItemClick("//$FOCUSED/JPEG");
            ctx->Yield(2);

            IM_CHECK(gState.settings.format == ImageFormat::Jpeg);
            IM_CHECK(ctx->ItemExists("Quality"));

            gState.settings.format = ImageFormat::Png;
            ctx->Yield(2);
        };

        t = IM_REGISTER_TEST(engine, "ui", "overlay_replaces_the_panels");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            // While a region is being chosen the overlay owns the window.
            // Panels drawn underneath would take clicks meant for the
            // selection.
            gState.phase = CapturePhase::Selecting;
            gState.backdrop = MakeTestImage(400, 300);
            gState.backdropBounds = Rect{0, 0, 400, 300};
            ctx->Yield(3);

            IM_CHECK(ImGui::FindWindowByName("##RegionOverlay") != nullptr);
            ImGuiWindow* capture = ImGui::FindWindowByName(ui::kWindowCapture);
            IM_CHECK(capture == nullptr || !capture->Active);

            gState.phase = CapturePhase::Idle;
            gState.backdrop.Reset();
            ctx->Yield(3);
            IM_CHECK(ImGui::FindWindowByName(ui::kWindowCapture)->Active);
        };

        t = IM_REGISTER_TEST(engine, "ui", "quit_menu_sets_the_flag");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            gState.quitRequested = false;
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("File/Quit");
            ctx->Yield(2);
            IM_CHECK(gState.quitRequested);
            gState.quitRequested = false;
        };
    }
}

int main(int argc, char** argv)
{
    std::string error;
    if (!paths::Init(argc > 0 ? argv[0] : nullptr, error))
    {
        std::printf("daveshot_test_ui: %s\n", error.c_str());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename   = nullptr;         // tests must not read or write a layout
    io.DisplaySize   = ImVec2(1600.0f, 1000.0f);
    io.DeltaTime     = 1.0f / 60.0f;
    io.ConfigFlags  |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    // Loading the real fonts is part of what these tests cover: if the
    // staging step stopped copying assets, this is where it shows up.
    std::string fontError;
    if (!fonts::Load(17.0f, fontError))
        std::printf("daveshot_test_ui: %s\n", fontError.c_str());

    theme::Apply(0, 1.0f);
    MarkStyleApplied(gState);

    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& tio  = ImGuiTestEngine_GetIO(engine);
    tio.ConfigRunSpeed            = ImGuiTestRunSpeed_Fast;
    tio.ConfigNoThrottle          = true;
    tio.ConfigVerboseLevel        = ImGuiTestVerboseLevel_Warning;
    tio.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    // Without this the engine keeps its log to itself, and a failing test on
    // a build machine prints nothing but a count.
    tio.ConfigLogToTTY = true;

    RegisterTests(engine);
    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, "all", ImGuiTestRunFlags_None);

    while (!ImGuiTestEngine_IsTestQueueEmpty(engine))
        RunOneFrame(engine);

    int total = 0, success = 0;
    ImGuiTestEngine_GetResult(engine, total, success);

    ImVector<ImGuiTest*> tests;
    ImGuiTestEngine_GetTestList(engine, &tests);
    for (ImGuiTest* test : tests)
    {
        const bool passed = (test->Output.Status == ImGuiTestStatus_Success);
        std::printf("%-4s %s/%s\n", passed ? "ok" : "FAIL", test->Category, test->Name);
    }
    std::printf("ui: %d tests, %d failed\n", total, total - success);

    ImGuiTestEngine_Stop(engine);
    gTextures.Clear();
    // DestroyContext on the engine requires ImGui's context to be gone first.
    ImGui::DestroyContext();
    ImGuiTestEngine_DestroyContext(engine);

    return (total == success) ? 0 : 1;
}
