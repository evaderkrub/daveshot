#include "ui/Panels.h"

#include "app/AppState.h"
#include "app/CaptureService.h"
#include "platform/Hotkeys.h"
#include "ui/Fonts.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/Theme.h"

#include "imgui.h"

#include <cstring>
#include <string>
#include <vector>

namespace daveshot::ui
{
namespace
{
    const char* FormatLabel(ImageFormat format)
    {
        return (format == ImageFormat::Jpeg) ? "JPEG" : "PNG";
    }

    // ImGui text fields work on a char buffer, and the settings hold
    // std::strings. Edit into a buffer sized for a long path and copy back
    // only when it actually changed.
    bool StringField(const char* label, std::string& value, size_t capacity = 512)
    {
        std::vector<char> buffer(capacity, 0);
        const size_t copied = value.size() < capacity - 1 ? value.size() : capacity - 1;
        std::memcpy(buffer.data(), value.data(), copied);

        if (!ImGui::InputText(label, buffer.data(), buffer.size()))
            return false;

        value.assign(buffer.data());
        return true;
    }

    void DrawHotkeyCombo(const char* label, std::string& value, bool& changed)
    {
        if (!ImGui::BeginCombo(label, value.c_str()))
            return;

        for (const std::string& preset : hotkeys::Presets())
        {
            const bool selected = (preset == value);
            if (ImGui::Selectable(preset.c_str(), selected))
            {
                value = preset;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    void DrawAppearance(AppState& state)
    {
        int themeIndex = theme::CurrentIndex();
        if (ImGui::BeginCombo("Theme", theme::At(themeIndex).name))
        {
            for (int i = 0; i < theme::Count(); ++i)
            {
                const bool selected = (i == themeIndex);
                if (ImGui::Selectable(theme::At(i).name, selected))
                    SetTheme(state, theme::At(i).name);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // The slider writes through SetUiScale so the style-rebuild flag is
        // raised in exactly one place; the rebuild itself happens between
        // frames, never here.
        float scale = state.settings.uiScale;
        if (ImGui::SliderFloat("UI scale", &scale,
                               Settings::kMinScale, Settings::kMaxScale, "%.2fx"))
            SetUiScale(state, scale);

        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##scale"))
            SetUiScale(state, 1.0f);

        ImGui::TextDisabled("Fonts loaded at %.0f px, drawn at %.0f px",
                            (double)fonts::BaseSize(),
                            (double)(fonts::BaseSize() * state.settings.uiScale));
    }

    void DrawSaving(AppState& state)
    {
        Settings& s = state.settings;

        std::string folder = capture::ResolveSaveFolder(s);
        if (StringField("Folder", folder))
            s.saveFolder = folder;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Where captures are written.\nLeave empty for the daveshot folder in Pictures.");

        StringField("Filename", s.filenamePattern, 128);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%%Y year  %%m month  %%d day\n%%H hour  %%M minute  %%S second");

        if (ImGui::BeginCombo("Format", FormatLabel(s.format)))
        {
            for (ImageFormat option : { ImageFormat::Png, ImageFormat::Jpeg })
            {
                const bool selected = (option == s.format);
                if (ImGui::Selectable(FormatLabel(option), selected))
                    s.format = option;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (s.format == ImageFormat::Jpeg)
        {
            int quality = s.jpegQuality;
            if (ImGui::SliderInt("Quality", &quality, 1, 100))
                s.jpegQuality = ClampJpegQuality(quality);
        }

        ImGui::Checkbox("Save automatically", &s.autoSave);
        ImGui::Checkbox("Copy to clipboard", &s.autoCopy);
    }

    void DrawCaptureBehaviour(AppState& state)
    {
        Settings& s = state.settings;

        ImGui::Checkbox("Hide daveshot while capturing", &s.hideOnCapture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keeps this window out of full-screen captures");

        bool changed = false;
        bool enabled = s.hotkeyEnabled;
        if (ImGui::Checkbox("Global hotkeys", &enabled))
            changed = true;

        ImGui::BeginDisabled(!enabled);
        std::string region = s.hotkeyRegion;
        std::string screen = s.hotkeyScreen;
        DrawHotkeyCombo("Region", region, changed);
        DrawHotkeyCombo("Full screen", screen, changed);
        ImGui::EndDisabled();

        if (changed)
            SetHotkeys(state, region, screen, enabled);

        int limit = s.historyLimit;
        if (ImGui::SliderInt("History", &limit, Settings::kMinHistory, Settings::kMaxHistory))
        {
            s.historyLimit = ClampHistoryLimit(limit);
            state.history.limit = s.historyLimit;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How many captures to keep in memory.\nA 4K capture is about 33 MB.");
    }
}

void DrawSettingsPanel(AppState& state)
{
    if (!state.showSettings)
        return;

    ImGui::SetNextWindowSize(ImVec2(380, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowSettings, &state.showSettings))
    {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText(ICON_MD_IMAGE "  Saving");
    DrawSaving(state);

    ImGui::SeparatorText(ICON_MD_KEYBOARD "  Capture");
    DrawCaptureBehaviour(state);

    ImGui::SeparatorText(ICON_MD_SETTINGS "  Appearance");
    DrawAppearance(state);

    ImGui::End();
}
}
