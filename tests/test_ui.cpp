// End-to-end interface tests.
//
// A real ImGui context with no window and no GPU, driven by the Dear ImGui
// Test Engine. The function under test is ui::Draw -- the same one the
// application calls -- so there is no separate, test-only interface to drift
// away from the real one.

#include "app/AppState.h"
#include "platform/Paths.h"
#include "ui/Fonts.h"
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
    AppState gState;

    // A renderer that never renders. Declaring RendererHasTextures and then
    // acknowledging every request is what lets the real fonts load here: the
    // atlas asks for a texture, we say "done", and glyph metrics -- which is
    // all the tests measure -- come out identical to the application's.
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
    // style rebuild sits before NewFrame here for the same reason it does
    // there: ScaleAllSizes is not idempotent and mid-frame metric changes
    // leave the rest of the frame measured against the old numbers.
    void RunOneFrame(ImGuiTestEngine* engine)
    {
        if (StyleNeedsRebuild(gState))
        {
            theme::Apply(theme::IndexByName(gState.settings.theme.c_str()),
                         gState.settings.uiScale);
            MarkStyleApplied(gState);
        }

        ImGui::NewFrame();
        ui::Draw(gState);
        ImGui::Render();
        HonorTextureRequests();

        // PostSwap is the engine's "frame presented" signal. With no swap to
        // wait for, it goes immediately after Render().
        ImGuiTestEngine_PostSwap(engine);
    }

    void RegisterTests(ImGuiTestEngine* engine)
    {
        ImGuiTest* t = nullptr;

        t = IM_REGISTER_TEST(engine, "ui", "opens_with_a_workspace");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            ctx->SetRef(ui::kWindowWorkspace);
            IM_CHECK(ctx->WindowInfo("").Window != nullptr);
            IM_CHECK(ctx->ItemExists("Capture"));
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

        t = IM_REGISTER_TEST(engine, "ui", "scale_changes_metrics");
        t->TestFunc = [](ImGuiTestContext* ctx)
        {
            SetUiScale(gState, 1.0f);
            ctx->Yield(2);
            const float basePadding = ImGui::GetStyle().FramePadding.x;
            const float baseFont    = ImGui::GetStyle().FontScaleMain;

            gState.showSettings = true;
            ctx->Yield(2);
            ctx->SetRef(ui::kWindowSettings);
            ctx->ItemInputValue("UI scale", 2.0f);
            ctx->Yield(2);

            IM_CHECK_EQ(gState.settings.uiScale, 2.0f);
            // Both halves of "scale the gui" have to move: the metrics and
            // the text. Checking only the slider value would pass even if the
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
            ctx->Yield(2);

            gState.showSettings = true;
            ctx->Yield(2);
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
    io.IniFilename  = nullptr;          // tests must not read or write a layout
    io.DisplaySize  = ImVec2(1440.0f, 900.0f);
    io.DeltaTime    = 1.0f / 60.0f;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
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
    tio.ConfigRunSpeed   = ImGuiTestRunSpeed_Fast;
    tio.ConfigNoThrottle = true;
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
    std::printf("ui: %d tests, %d failed\n", total, total - success);

    ImGuiTestEngine_Stop(engine);
    // DestroyContext on the engine requires ImGui's context to be gone first.
    ImGui::DestroyContext();
    ImGuiTestEngine_DestroyContext(engine);

    return (total == success) ? 0 : 1;
}
