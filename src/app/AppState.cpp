#include "app/AppState.h"

#include <cmath>

namespace daveshot
{
void SetUiScale(AppState& state, float scale)
{
    const float clamped = ClampUiScale(scale);
    if (clamped == state.settings.uiScale)
        return;
    state.settings.uiScale = clamped;
    ++state.styleRevision;
}

void SetTheme(AppState& state, const std::string& themeName)
{
    if (themeName.empty() || themeName == state.settings.theme)
        return;
    state.settings.theme = themeName;
    ++state.styleRevision;
}

void SetHotkeys(AppState& state, const std::string& region, const std::string& screen,
                bool enabled)
{
    if (region == state.settings.hotkeyRegion &&
        screen == state.settings.hotkeyScreen &&
        enabled == state.settings.hotkeyEnabled)
        return;

    if (!region.empty()) state.settings.hotkeyRegion = region;
    if (!screen.empty()) state.settings.hotkeyScreen = screen;
    state.settings.hotkeyEnabled = enabled;
    ++state.hotkeyRevision;
}

bool StyleNeedsRebuild(const AppState& state)
{
    return state.styleRevision != state.appliedStyleRevision;
}

void MarkStyleApplied(AppState& state)
{
    state.appliedStyleRevision = state.styleRevision;
}

bool HotkeysNeedRegistering(const AppState& state)
{
    return state.hotkeyRevision != state.appliedHotkeyRevision;
}

void MarkHotkeysApplied(AppState& state)
{
    state.appliedHotkeyRevision = state.hotkeyRevision;
}

void ReportError(AppState& state, const std::string& message)
{
    state.error = message;
}

const Shot* CurrentShot(const AppState& state)
{
    if (state.history.shots.empty())
        return nullptr;
    if (state.selectedShotId != 0)
    {
        if (const Shot* found = state.history.Find(state.selectedShotId))
            return found;
    }
    return &state.history.shots.front();
}

Shot* CurrentShot(AppState& state)
{
    return const_cast<Shot*>(CurrentShot(const_cast<const AppState&>(state)));
}

Shot& AcceptShot(AppState& state, Shot shot)
{
    state.history.limit = ClampHistoryLimit(state.settings.historyLimit);
    const unsigned id = shot.id;
    state.history.Add(std::move(shot));
    state.selectedShotId = id;

    // Find() rather than front(): a limit of 1 combined with an Add that
    // trims could leave the shot we just took as the only entry, and either
    // way the id is the thing that identifies it.
    Shot* stored = state.history.Find(id);
    return stored ? *stored : state.history.shots.front();
}

bool CaptureInProgress(const AppState& state)
{
    return state.phase != CapturePhase::Idle;
}

int CountdownRemaining(const AppState& state, double now)
{
    if (state.phase != CapturePhase::Countdown)
        return 0;
    const double left = state.countdownEnd - now;
    if (left <= 0.0)
        return 0;
    return (int)std::ceil(left);
}
}
