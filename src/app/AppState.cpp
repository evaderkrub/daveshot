#include "app/AppState.h"

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

bool StyleNeedsRebuild(const AppState& state)
{
    return state.styleRevision != state.appliedStyleRevision;
}

void MarkStyleApplied(AppState& state)
{
    state.appliedStyleRevision = state.styleRevision;
}

void ReportError(AppState& state, const std::string& message)
{
    state.error = message;
}
}
