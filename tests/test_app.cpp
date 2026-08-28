// Application-state tests. No window, no ImGui context: everything here is
// the plain-struct half of the application, which is the point of keeping it
// plain.

#include "Check.h"

#include "app/AppState.h"
#include "app/Settings.h"
#include "platform/Paths.h"

#include <cmath>
#include <string>

using namespace daveshot;

namespace
{
void TestScaleClamping()
{
    CHECK_NEAR(ClampUiScale(1.0f), 1.0f);
    CHECK_NEAR(ClampUiScale(0.1f), Settings::kMinScale);
    CHECK_NEAR(ClampUiScale(99.0f), Settings::kMaxScale);

    // A settings file edited by hand can contain anything; NaN must land on
    // the default rather than propagate into the style metrics.
    CHECK_NEAR(ClampUiScale(std::nanf("")), 1.0f);
}

void TestSettingsParsing()
{
    Settings s;
    CHECK(ParseSettings("uiscale=1.5\ntheme=Nord\n", s));
    CHECK_NEAR(s.uiScale, 1.5f);
    CHECK(s.theme == "Nord");

    // Comments, blank lines, whitespace and unknown keys are all ignored, so
    // a file written by a newer build still loads in an older one.
    Settings t;
    CHECK(ParseSettings("# comment\n\n  uiscale = 2.0  \nfuture=42\n", t));
    CHECK_NEAR(t.uiScale, 2.0f);
    CHECK(t.theme == Settings().theme);

    // Out-of-range values are clamped on the way in, not rejected.
    Settings u;
    CHECK(ParseSettings("uiscale=17\n", u));
    CHECK_NEAR(u.uiScale, Settings::kMaxScale);

    // An empty theme name would leave the combo with nothing to show.
    Settings v;
    CHECK(ParseSettings("theme=\n", v));
    CHECK(v.theme == Settings().theme);
}

void TestSettingsRoundTrip()
{
    Settings written;
    written.uiScale = 1.25f;
    written.theme   = "Midnight";

    Settings read;
    CHECK(ParseSettings(SerializeSettings(written), read));
    CHECK_NEAR(read.uiScale, written.uiScale);
    CHECK(read.theme == written.theme);
}

void TestSettingsFileIo()
{
    const std::string path = paths::Beside("daveshot_settings_test.txt");

    Settings written;
    written.uiScale = 1.75f;
    written.theme   = "Light";

    std::string error;
    CHECK(SaveSettings(path, written, error));
    CHECK(error.empty());

    Settings read;
    CHECK(LoadSettings(path, read, error));
    CHECK_NEAR(read.uiScale, 1.75f);
    CHECK(read.theme == "Light");

    paths::RemoveFile(path);

    // A missing file is first-run, not an error: defaults must survive.
    Settings fresh;
    CHECK(LoadSettings(path, fresh, error));
    CHECK(error.empty());
    CHECK_NEAR(fresh.uiScale, 1.0f);
}

void TestStyleRevision()
{
    AppState state;
    CHECK(StyleNeedsRebuild(state));   // nothing applied yet

    MarkStyleApplied(state);
    CHECK(!StyleNeedsRebuild(state));

    SetUiScale(state, 1.5f);
    CHECK(StyleNeedsRebuild(state));
    CHECK_NEAR(state.settings.uiScale, 1.5f);

    // Setting the same value again must not schedule a rebuild: the rebuild
    // replaces the whole style, and doing it every frame is visible.
    MarkStyleApplied(state);
    SetUiScale(state, 1.5f);
    CHECK(!StyleNeedsRebuild(state));

    SetTheme(state, "Nord");
    CHECK(StyleNeedsRebuild(state));
    MarkStyleApplied(state);
    SetTheme(state, "Nord");
    CHECK(!StyleNeedsRebuild(state));

    SetTheme(state, "");
    CHECK(!StyleNeedsRebuild(state));
    CHECK(state.settings.theme == "Nord");
}

void TestPaths()
{
    // Every runtime path hangs off the executable's directory, never the
    // working directory -- that is what makes the staged folder portable.
    CHECK(!paths::ExeDir().empty());
    CHECK(paths::Asset("fonts/OpenSans-Regular.ttf")
              .rfind(paths::ExeDir(), 0) == 0);
    CHECK(paths::Beside("x.txt") == paths::ExeDir() + "/x.txt");

    // The staging step should have put the fonts beside this binary.
    CHECK(paths::Exists(paths::Asset("fonts/OpenSans-Regular.ttf")));
    CHECK(paths::Exists(paths::Asset("icons/MaterialIcons-Regular.ttf")));
    CHECK(!paths::Exists(paths::Asset("fonts/definitely-not-here.ttf")));
}
}

int main(int argc, char** argv)
{
    std::string error;
    if (!paths::Init(argc > 0 ? argv[0] : nullptr, error))
    {
        std::printf("FAIL could not resolve the executable directory: %s\n", error.c_str());
        return 1;
    }

    TestScaleClamping();
    TestSettingsParsing();
    TestSettingsRoundTrip();
    TestSettingsFileIo();
    TestStyleRevision();
    TestPaths();

    return daveshot::testing::Summary("app");
}
