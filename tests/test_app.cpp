// Application-state tests. No window, no ImGui context, no screen: this is
// the plain-struct half of the application, which is the point of keeping it
// plain.

#include "Check.h"

#include "app/AppState.h"
#include "app/Capture.h"
#include "app/CaptureFlow.h"
#include "app/Image.h"
#include "app/Naming.h"
#include "app/Settings.h"
#include "platform/Hotkeys.h"
#include "platform/Paths.h"

#include <cmath>
#include <string>

using namespace daveshot;

namespace
{
// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void TestScaleClamping()
{
    CHECK_NEAR(ClampUiScale(1.0f), 1.0f);
    CHECK_NEAR(ClampUiScale(0.1f), Settings::kMinScale);
    CHECK_NEAR(ClampUiScale(99.0f), Settings::kMaxScale);

    // A settings file edited by hand can contain anything; NaN must land on
    // the default rather than propagate into the style metrics.
    CHECK_NEAR(ClampUiScale(std::nanf("")), 1.0f);

    CHECK(ClampDelaySeconds(-4) == 0);
    CHECK(ClampDelaySeconds(999) == Settings::kMaxDelaySeconds);
    CHECK(ClampJpegQuality(0) == 1);
    CHECK(ClampJpegQuality(500) == 100);
    CHECK(ClampHistoryLimit(0) == Settings::kMinHistory);
    CHECK(ClampHistoryLimit(9999) == Settings::kMaxHistory);
}

void TestSettingsParsing()
{
    Settings s;
    CHECK(ParseSettings("uiscale=1.5\ntheme=Nord\nformat=jpeg\njpegquality=55\n", s));
    CHECK_NEAR(s.uiScale, 1.5f);
    CHECK(s.theme == "Nord");
    CHECK(s.format == ImageFormat::Jpeg);
    CHECK(s.jpegQuality == 55);

    // Comments, blank lines, whitespace and unknown keys are all ignored, so
    // a file written by a newer build still loads in an older one.
    Settings t;
    CHECK(ParseSettings("# comment\n\n  uiscale = 2.0  \nfuture=42\n", t));
    CHECK_NEAR(t.uiScale, 2.0f);
    CHECK(t.theme == Settings().theme);

    // Out-of-range values are clamped on the way in, not rejected.
    Settings u;
    CHECK(ParseSettings("uiscale=17\ndelayseconds=-3\njpegquality=999\n", u));
    CHECK_NEAR(u.uiScale, Settings::kMaxScale);
    CHECK(u.delaySeconds == 0);
    CHECK(u.jpegQuality == 100);

    // Empty values would leave a combo with nothing to show, or a filename
    // with nothing to be.
    Settings v;
    CHECK(ParseSettings("theme=\nfilenamepattern=\nhotkeyregion=\n", v));
    CHECK(v.theme == Settings().theme);
    CHECK(v.filenamePattern == Settings().filenamePattern);
    CHECK(v.hotkeyRegion == Settings().hotkeyRegion);

    // An unrecognised format name is PNG, not a broken save later on.
    Settings w;
    CHECK(ParseSettings("format=tiff\n", w));
    CHECK(w.format == ImageFormat::Png);

    // Booleans accept the spellings a person would actually type.
    Settings b;
    CHECK(ParseSettings("autosave=no\nautocopy=1\nhideoncapture=false\n", b));
    CHECK(!b.autoSave);
    CHECK(b.autoCopy);
    CHECK(!b.hideOnCapture);
}

void TestSettingsRoundTrip()
{
    Settings written;
    written.uiScale         = 1.25f;
    written.theme           = "Midnight";
    written.saveFolder      = "D:/shots";
    written.filenamePattern = "shot-%Y%m%d";
    written.format          = ImageFormat::Jpeg;
    written.jpegQuality     = 72;
    written.autoSave        = false;
    written.autoCopy        = false;
    written.delaySeconds    = 5;
    written.hideOnCapture   = false;
    written.hotkeyEnabled   = false;
    written.hotkeyRegion    = "Ctrl+Alt+S";
    written.hotkeyScreen    = "None";
    written.closeToTray     = false;
    written.historyLimit    = 25;

    Settings read;
    CHECK(ParseSettings(SerializeSettings(written), read));
    CHECK_NEAR(read.uiScale, written.uiScale);
    CHECK(read.theme == written.theme);
    CHECK(read.saveFolder == written.saveFolder);
    CHECK(read.filenamePattern == written.filenamePattern);
    CHECK(read.format == written.format);
    CHECK(read.jpegQuality == written.jpegQuality);
    CHECK(read.autoSave == written.autoSave);
    CHECK(read.autoCopy == written.autoCopy);
    CHECK(read.delaySeconds == written.delaySeconds);
    CHECK(read.hideOnCapture == written.hideOnCapture);
    CHECK(read.hotkeyEnabled == written.hotkeyEnabled);
    CHECK(read.hotkeyRegion == written.hotkeyRegion);
    CHECK(read.hotkeyScreen == written.hotkeyScreen);
    CHECK(read.closeToTray == written.closeToTray);
    CHECK(read.historyLimit == written.historyLimit);
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

// ---------------------------------------------------------------------------
// Filenames
// ---------------------------------------------------------------------------
void TestPatternExpansion()
{
    TimeParts when;
    when.year = 2026; when.month  = 3; when.day    = 7;
    when.hour = 9;    when.minute = 4; when.second = 5;

    CHECK_STR(ExpandPattern("daveshot_%Y-%m-%d_%H-%M-%S", when).c_str(),
              "daveshot_2026-03-07_09-04-05");

    // Padding matters: unpadded numbers sort wrongly in a file listing, which
    // is most of what a timestamped name is for.
    CHECK_STR(ExpandPattern("%H%M%S", when).c_str(), "090405");
    CHECK_STR(ExpandPattern("100%% sure", when).c_str(), "100% sure");

    // An unknown token survives verbatim, so a typo is visible rather than
    // silently swallowed.
    CHECK_STR(ExpandPattern("shot%q", when).c_str(), "shot%q");

    // A pattern is a filename, not a path: separators and the rest of the
    // forbidden set are neutralised so the save cannot fail on the name.
    CHECK_STR(ExpandPattern("a/b\\c:d*e?f", when).c_str(), "a-b-c-d-e-f");

    // Windows cannot hold a name that is empty or ends in a dot or a space.
    CHECK_STR(ExpandPattern("", when).c_str(), "daveshot");
    CHECK_STR(ExpandPattern("  ..shot..  ", when).c_str(), "shot");
    CHECK_STR(ExpandPattern("***", when).c_str(), "---");
}

void TestPathJoining()
{
    CHECK_STR(JoinPath("C:/pics", "a.png").c_str(), "C:/pics/a.png");
    CHECK_STR(JoinPath("C:/pics/", "a.png").c_str(), "C:/pics/a.png");
    CHECK_STR(JoinPath("C:\\pics\\", "a.png").c_str(), "C:\\pics\\a.png");
    CHECK_STR(JoinPath("", "a.png").c_str(), "a.png");
}

void TestUniquePath()
{
    const std::string folder = paths::ExeDir();
    const std::string stem   = "daveshot_unique_test";

    const std::string first = UniquePath(folder, stem, "png");
    CHECK_STR(first.c_str(), JoinPath(folder, stem + ".png").c_str());

    // With the name taken, the next one has to differ -- two captures inside
    // the same second is the ordinary way to hit this, and overwriting the
    // first would be the worst possible answer.
    std::FILE* f = paths::OpenFile(first, "wb");
    CHECK(f != nullptr);
    if (f) std::fclose(f);

    const std::string second = UniquePath(folder, stem, "png");
    CHECK(second != first);
    CHECK_STR(second.c_str(), JoinPath(folder, stem + " (2).png").c_str());

    paths::RemoveFile(first);
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------
Image MakeTestImage(int width, int height)
{
    Image image;
    image.width  = width;
    image.height = height;
    image.pixels.assign((size_t)width * (size_t)height * 4u, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t* px = image.pixels.data() + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            px[0] = (uint8_t)x;
            px[1] = (uint8_t)y;
            px[2] = 128;
            px[3] = 255;
        }
    return image;
}

void TestGeometry()
{
    // A drag that runs right to left or bottom to top is still a rectangle.
    const Rect a = RectFromCorners(100, 80, 20, 10);
    CHECK(a.x == 20 && a.y == 10 && a.w == 80 && a.h == 70);

    const Rect b = RectFromCorners(5, 5, 5, 5);
    CHECK(b.Empty());

    CHECK(Intersect(Rect{0, 0, 100, 100}, Rect{50, 50, 100, 100}) == (Rect{50, 50, 50, 50}));
    CHECK(Intersect(Rect{0, 0, 10, 10}, Rect{20, 20, 10, 10}).Empty());
    CHECK((Rect{0, 0, 10, 10}).Contains(9, 9));
    CHECK(!(Rect{0, 0, 10, 10}).Contains(10, 10));
}

void TestCrop()
{
    const Image source = MakeTestImage(16, 8);

    Image out;
    CHECK(CropImage(source, Rect{4, 2, 5, 3}, out));
    CHECK(out.width == 5 && out.height == 3);
    CHECK(out.Valid());
    // The top-left of the crop is source pixel (4,2).
    CHECK(out.pixels[0] == 4);
    CHECK(out.pixels[1] == 2);

    // A selection running off the edge is clipped, not refused: a drag that
    // ends past the desktop edge is normal.
    Image clipped;
    CHECK(CropImage(source, Rect{12, 6, 100, 100}, clipped));
    CHECK(clipped.width == 4 && clipped.height == 2);

    Image none;
    CHECK(!CropImage(source, Rect{100, 100, 10, 10}, none));
}

void TestScale()
{
    const Image source = MakeTestImage(64, 32);

    Image thumb;
    CHECK(ScaleImageToFit(source, 16, 16, thumb));
    CHECK(thumb.width == 16 && thumb.height == 8);   // aspect kept
    CHECK(thumb.Valid());

    // Already small enough: copied, not blown up.
    Image same;
    CHECK(ScaleImageToFit(source, 500, 500, same));
    CHECK(same.width == 64 && same.height == 32);
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
void TestHistory()
{
    AppState state;
    state.settings.historyLimit = 3;
    state.history.limit = 3;

    CHECK(CurrentShot(state) == nullptr);

    unsigned ids[5] = {};
    for (int i = 0; i < 5; ++i)
    {
        Shot shot = MakeShot(MakeTestImage(20, 10), CaptureMode::Region,
                             Rect{0, 0, 20, 10}, LocalTimeNow());
        ids[i] = shot.id;
        AcceptShot(state, std::move(shot));
    }

    // Newest first, oldest dropped.
    CHECK((int)state.history.shots.size() == 3);
    CHECK(state.history.shots.front().id == ids[4]);
    CHECK(state.history.Find(ids[0]) == nullptr);
    CHECK(state.history.Find(ids[4]) != nullptr);

    // The newest capture is what the preview shows.
    const Shot* current = CurrentShot(state);
    CHECK(current != nullptr && current->id == ids[4]);

    // Selecting an older one sticks.
    state.selectedShotId = ids[3];
    current = CurrentShot(state);
    CHECK(current != nullptr && current->id == ids[3]);

    // A selection that has fallen out of the history falls back to the newest
    // rather than showing nothing.
    state.selectedShotId = ids[0];
    current = CurrentShot(state);
    CHECK(current != nullptr && current->id == ids[4]);

    CHECK(state.history.TotalBytes() > 0);
    state.history.Clear();
    CHECK(CurrentShot(state) == nullptr);
}

void TestShotLabels()
{
    const Shot shot = MakeShot(MakeTestImage(300, 200), CaptureMode::Window,
                               Rect{0, 0, 300, 200}, LocalTimeNow());
    CHECK_STR(shot.label.c_str(), "Window 300x200");
    CHECK(shot.thumbnail.Valid());
    CHECK(shot.thumbnail.width <= kThumbnailSize);
    CHECK(shot.thumbnail.height <= kThumbnailSize);

    const Shot other = MakeShot(MakeTestImage(4, 4), CaptureMode::Region,
                                Rect{}, LocalTimeNow());
    CHECK(other.id != shot.id);   // ids are never reused
}

// ---------------------------------------------------------------------------
// The capture sequence
// ---------------------------------------------------------------------------
// A stand-in for the window and the screen, so the ordering rules can be
// tested without either.
class FakeEffects final : public CaptureEffects
{
public:
    int  hides = 0, shows = 0, overlays = 0, leaves = 0, grabs = 0, desktopGrabs = 0;
    int  permissionAsks = 0;
    bool grabSucceeds = true;
    bool permissionNeeded = false;    // as on Windows and X11
    bool permissionGranted = true;
    Rect overlayCovers;   // empty: the whole desktop, as on Windows and X11

    void HideWindow() override { ++hides; }
    void ShowWindow() override { ++shows; }

    bool NeedsCapturePermission() override { return permissionNeeded; }

    bool RequestCapturePermission(std::string& error) override
    {
        ++permissionAsks;
        if (!permissionGranted) { error = "permission refused"; return false; }
        permissionNeeded = false;
        return true;
    }

    bool EnterOverlay(const Rect& desktop, Rect& covered, std::string& error) override
    {
        ++overlays;
        covered = overlayCovers.Empty() ? desktop : overlayCovers;
        if (!grabSucceeds) { error = "no overlay"; return false; }
        return true;
    }
    void LeaveOverlay() override { ++leaves; }

    bool Grab(const CaptureRequest&, Image& out, Rect& source, std::string& error) override
    {
        ++grabs;
        if (!grabSucceeds) { error = "grab refused"; return false; }
        out = MakeTestImage(40, 30);
        source = Rect{0, 0, 40, 30};
        return true;
    }

    bool GrabDesktop(Image& out, Rect& bounds, std::string& error) override
    {
        ++desktopGrabs;
        if (!grabSucceeds) { error = "desktop grab refused"; return false; }
        out = MakeTestImage(200, 100);
        bounds = Rect{-50, 0, 200, 100};
        return true;
    }
};

AppState QuietState()
{
    AppState state;
    // These tests are about sequencing, not about writing files or reaching
    // into the clipboard of whoever is running them.
    state.settings.autoSave = false;
    state.settings.autoCopy = false;
    return state;
}

void TestDirectCaptureSequence()
{
    AppState state = QuietState();
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;

    RequestCapture(state, request, 100.0);
    CHECK(state.phase == CapturePhase::Hiding);
    CHECK(CaptureInProgress(state));

    // The desktop gets a moment to repaint without our window in it. Firing
    // early is exactly how a screenshot tool ends up in its own screenshot.
    TickCapture(state, effects, 100.0);
    CHECK(effects.hides > 0);
    CHECK(effects.grabs == 0);
    CHECK(state.phase == CapturePhase::Hiding);

    TickCapture(state, effects, 100.0 + kHideSettleSeconds);
    CHECK(effects.grabs == 1);
    CHECK(effects.shows == 1);
    CHECK(state.phase == CapturePhase::Idle);
    CHECK((int)state.history.shots.size() == 1);
}

void TestDelayCountdown()
{
    AppState state = QuietState();
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;
    request.delaySeconds = 3;

    RequestCapture(state, request, 10.0);
    CHECK(state.phase == CapturePhase::Countdown);
    CHECK(CountdownRemaining(state, 10.0) == 3);
    CHECK(CountdownRemaining(state, 12.5) == 1);

    // The window must stay visible through the countdown -- the point of a
    // delay is to set something up while you can still see the screen.
    TickCapture(state, effects, 11.0);
    CHECK(state.phase == CapturePhase::Countdown);
    CHECK(effects.hides == 0);

    TickCapture(state, effects, 13.0);
    CHECK(state.phase == CapturePhase::Hiding);
    CHECK(CountdownRemaining(state, 13.0) == 0);

    TickCapture(state, effects, 13.0 + kHideSettleSeconds);
    CHECK(state.phase == CapturePhase::Idle);
    CHECK(effects.grabs == 1);
}

// Wayland's portal will not photograph the screen until the user has said
// so, and the desktop only puts that question up while our window is up --
// so the asking has to happen before the first hide, and the shot has to
// wait out a fresh settle afterwards rather than firing against a clock that
// went stale while the dialog was open.
void TestPermissionIsAskedBeforeHiding()
{
    AppState state = QuietState();
    FakeEffects effects;
    effects.permissionNeeded = true;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;

    RequestCapture(state, request, 100.0);
    TickCapture(state, effects, 100.0);
    CHECK(effects.permissionAsks == 1);
    CHECK(effects.hides == 0);      // asked with the window still on screen
    CHECK(effects.grabs == 0);

    // The user has been sitting on the dialog; the clock has moved on.
    TickCapture(state, effects, 130.0);
    CHECK(effects.permissionAsks == 1);   // never asked twice
    CHECK(state.phase == CapturePhase::Hiding);

    // The settle is measured from here and not from the stale 100: at half a
    // settle past 130 the window has hidden but the shot has not been taken.
    TickCapture(state, effects, 130.0 + kHideSettleSeconds * 0.5);
    CHECK(effects.hides > 0);
    CHECK(effects.grabs == 0);

    TickCapture(state, effects, 130.0 + kHideSettleSeconds);
    CHECK(effects.grabs == 1);
    CHECK(state.phase == CapturePhase::Idle);
    CHECK((int)state.history.shots.size() == 1);
}

void TestRefusedPermissionEndsTheCapture()
{
    AppState state = QuietState();
    FakeEffects effects;
    effects.permissionNeeded  = true;
    effects.permissionGranted = false;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;

    RequestCapture(state, request, 0.0);
    TickCapture(state, effects, 0.0);

    CHECK(effects.permissionAsks == 1);
    CHECK(effects.grabs == 0);
    CHECK(state.phase == CapturePhase::Idle);
    CHECK(effects.shows == 1);            // the window comes back either way
    CHECK(!state.error.empty());
    CHECK((int)state.history.shots.size() == 0);
}

void TestSecondRequestIsIgnored()
{
    AppState state = QuietState();
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;
    request.delaySeconds = 5;

    RequestCapture(state, request, 0.0);
    const double firstDeadline = state.countdownEnd;

    // Leaning on the hotkey during a countdown should not restart it or queue
    // a second shot.
    RequestCapture(state, request, 2.0);
    CHECK(state.countdownEnd == firstDeadline);

    TickCapture(state, effects, 6.0);
    TickCapture(state, effects, 6.0 + kHideSettleSeconds);
    CHECK(effects.grabs == 1);
    CHECK((int)state.history.shots.size() == 1);
}

void TestRegionSequence()
{
    AppState state = QuietState();
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::Region;

    RequestCapture(state, request, 0.0);
    TickCapture(state, effects, kHideSettleSeconds);

    CHECK(state.phase == CapturePhase::Selecting);
    CHECK(effects.desktopGrabs == 1);
    CHECK(effects.overlays == 1);
    CHECK(state.backdrop.Valid());
    CHECK(state.backdropBounds.x == -50);

    // The overlay works in image space; the shot records where on the desktop
    // it came from, which is offset by the desktop origin.
    state.selection = Rect{10, 20, 60, 40};
    CommitSelection(state, effects);

    CHECK(state.phase == CapturePhase::Idle);
    CHECK(effects.leaves == 1);
    CHECK(effects.shows == 1);
    CHECK(!state.backdrop.Valid());       // the biggest buffer we hold is released

    CHECK((int)state.history.shots.size() == 1);
    const Shot& shot = state.history.shots.front();
    CHECK(shot.image.width == 60 && shot.image.height == 40);
    CHECK(shot.source.x == -40);          // 10 + (-50)
    CHECK(shot.source.y == 20);
}

// Wayland can only put the overlay on one display. The sequence then has
// to show that display's part of the frozen desktop and keep the selection
// in the same coordinates, or a drag on the second monitor would crop pixels
// from the first.
void TestOverlayOnOneDisplayCropsTheBackdrop()
{
    AppState state = QuietState();
    FakeEffects effects;
    // The fake desktop is 200x100 at x=-50; the overlay covers only the
    // right-hand 120x100 of it.
    effects.overlayCovers = Rect{30, 0, 120, 100};

    CaptureRequest request;
    request.mode = CaptureMode::Region;
    RequestCapture(state, request, 0.0);
    TickCapture(state, effects, kHideSettleSeconds);

    CHECK(state.phase == CapturePhase::Selecting);
    CHECK(state.backdrop.width == 120 && state.backdrop.height == 100);
    CHECK(state.backdropBounds == (Rect{30, 0, 120, 100}));

    state.selection = Rect{10, 20, 60, 40};
    CommitSelection(state, effects);

    CHECK((int)state.history.shots.size() == 1);
    const Shot& shot = state.history.shots.front();
    CHECK(shot.image.width == 60 && shot.image.height == 40);
    CHECK(shot.source.x == 40);           // 10 + 30: relative to what the overlay showed
    CHECK(shot.source.y == 20);

    // An overlay that lands somewhere the capture does not cover at all is
    // an error, and the window still comes back.
    AppState missed = QuietState();
    FakeEffects elsewhere;
    elsewhere.overlayCovers = Rect{5000, 5000, 100, 100};
    RequestCapture(missed, request, 0.0);
    TickCapture(missed, elsewhere, kHideSettleSeconds);
    CHECK(missed.phase == CapturePhase::Idle);
    CHECK(!missed.error.empty());
    CHECK(elsewhere.leaves == 1);
    CHECK(elsewhere.shows == 1);
}

void TestTinySelectionIsNotAShot()
{
    AppState state = QuietState();
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::Region;
    RequestCapture(state, request, 0.0);
    TickCapture(state, effects, kHideSettleSeconds);

    // A stray click is not a two-pixel capture. The overlay stays up so the
    // user can try again.
    state.selection = Rect{5, 5, 2, 2};
    CommitSelection(state, effects);

    CHECK(state.phase == CapturePhase::Selecting);
    CHECK(state.history.shots.empty());
}

void TestCancelRestoresTheWindow()
{
    AppState state = QuietState();
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::Region;
    RequestCapture(state, request, 0.0);
    TickCapture(state, effects, kHideSettleSeconds);
    CHECK(state.phase == CapturePhase::Selecting);

    CancelCapture(state, effects);
    CHECK(state.phase == CapturePhase::Idle);
    CHECK(effects.leaves == 1);
    CHECK(effects.shows == 1);
    CHECK(state.history.shots.empty());
    CHECK(!state.backdrop.Valid());
}

void TestFailedGrabIsReportedAndRecovers()
{
    AppState state = QuietState();
    FakeEffects effects;
    effects.grabSucceeds = false;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;
    RequestCapture(state, request, 0.0);
    TickCapture(state, effects, kHideSettleSeconds);

    // The window has to come back even when the capture failed, or the
    // application is simply gone from the screen with no way to get it back.
    CHECK(state.phase == CapturePhase::Idle);
    CHECK(effects.shows == 1);
    CHECK(!state.error.empty());
    CHECK(state.history.shots.empty());
}

// ---------------------------------------------------------------------------
// Style and hotkey revisions
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Living in the tray
// ---------------------------------------------------------------------------
// A capture that starts with the window put away ends with it put away: the
// person pressed a key for a picture, not for a window.
void TestCaptureFromTheTrayStaysThere()
{
    AppState state = QuietState();
    state.settings.autoSave = true;   // a cancel saves nothing, so nothing is written
    state.inTray = true;
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::Region;
    RequestCapture(state, request, 10.0);
    TickCapture(state, effects, 10.0 + kHideSettleSeconds + 0.01);
    CHECK(state.phase == CapturePhase::Selecting);
    CHECK(effects.overlays == 1);

    CancelCapture(state, effects);
    CHECK(state.phase == CapturePhase::Idle);
    CHECK(effects.leaves == 1);
    CHECK(effects.shows == 0);
    CHECK(state.inTray);

    // The hide has to come before the overlay comes down, or the ordinary
    // window is on screen for a frame between the two.
    CHECK(effects.hides >= 1);
}

// A cancel leaves nothing behind, so it never needs the window -- whatever
// the copy and save settings say.
void TestCancelledCaptureFromTheTrayStaysThere()
{
    AppState state = QuietState();   // neither auto-copy nor auto-save
    state.inTray = true;
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::Region;
    RequestCapture(state, request, 10.0);
    TickCapture(state, effects, 10.0 + kHideSettleSeconds + 0.01);
    CancelCapture(state, effects);

    CHECK(effects.shows == 0);
    CHECK(state.inTray);
}

// A shot that is neither copied nor saved exists only in the window, so the
// window has to come back for it.
void TestUnkeptShotBringsTheWindowBack()
{
    AppState state = QuietState();
    state.inTray = true;
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::FullScreen;
    RequestCapture(state, request, 10.0);
    TickCapture(state, effects, 10.0 + kHideSettleSeconds + 0.01);

    CHECK(state.phase == CapturePhase::Idle);
    CHECK(state.history.shots.size() == 1);
    CHECK(effects.shows == 1);
    CHECK(!state.inTray);
}

// With the window up, nothing about the tray applies: the sequence ends by
// showing the window, as it always did.
void TestCaptureWithTheWindowUpShowsIt()
{
    AppState state = QuietState();
    state.settings.autoSave = true;
    state.inTray = false;
    FakeEffects effects;

    CaptureRequest request;
    request.mode = CaptureMode::Region;
    RequestCapture(state, request, 10.0);
    TickCapture(state, effects, 10.0 + kHideSettleSeconds + 0.01);
    CancelCapture(state, effects);

    CHECK(effects.shows == 1);
    CHECK(!state.inTray);
}

void TestRevisions()
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

void TestHotkeyRevisions()
{
    AppState state;
    MarkHotkeysApplied(state);
    CHECK(!HotkeysNeedRegistering(state));

    SetHotkeys(state, "Ctrl+Shift+A", state.settings.hotkeyScreen, true);
    CHECK(HotkeysNeedRegistering(state));
    CHECK(state.settings.hotkeyRegion == "Ctrl+Shift+A");

    // Re-registering costs an OS call per hotkey and can transiently fail, so
    // an unchanged setting must not trigger one.
    MarkHotkeysApplied(state);
    SetHotkeys(state, "Ctrl+Shift+A", state.settings.hotkeyScreen, true);
    CHECK(!HotkeysNeedRegistering(state));

    SetHotkeys(state, "Ctrl+Shift+A", state.settings.hotkeyScreen, false);
    CHECK(HotkeysNeedRegistering(state));
    CHECK(!state.settings.hotkeyEnabled);
}

// Which combination the desktop's own screenshot tool also answers. Only a
// bare Print does; getting this wrong would offer to change a system setting
// for a hotkey that never collided with one.
void TestBarePrintScreen()
{
    CHECK(hotkeys::IsBarePrintScreen("PrintScreen"));
    CHECK(hotkeys::IsBarePrintScreen("printscreen"));
    CHECK(hotkeys::IsBarePrintScreen(" Print "));
    CHECK(hotkeys::IsBarePrintScreen("PrtSc"));

    CHECK(!hotkeys::IsBarePrintScreen("Alt+PrintScreen"));
    CHECK(!hotkeys::IsBarePrintScreen("Ctrl+PrintScreen"));
    CHECK(!hotkeys::IsBarePrintScreen("Ctrl+Shift+S"));
    CHECK(!hotkeys::IsBarePrintScreen("None"));
    CHECK(!hotkeys::IsBarePrintScreen(""));
    CHECK(!hotkeys::IsBarePrintScreen("nonsense"));
}

// The cached answer to "who has Print Screen" is only valid for the hotkeys
// it was read for, so changing them has to invalidate it.
void TestPrintKeyGoesStaleWithTheHotkeys()
{
    AppState state;
    state.printKeyStale = false;

    SetHotkeys(state, "PrintScreen", state.settings.hotkeyScreen, true);
    CHECK(state.printKeyStale);

    state.printKeyStale = false;
    SetHotkeys(state, "PrintScreen", state.settings.hotkeyScreen, true);
    CHECK(!state.printKeyStale);   // nothing changed, nothing to re-read
}

// Freeing the key from the desktop does not change which combination we
// want -- it changes whether we can have it, so the loop has to ask again.
void TestRefreshHotkeysAsksAgainWithoutChangingAnything()
{
    AppState state;
    MarkHotkeysApplied(state);
    const std::string region = state.settings.hotkeyRegion;

    RefreshHotkeys(state);
    CHECK(HotkeysNeedRegistering(state));
    CHECK(state.settings.hotkeyRegion == region);
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

    CHECK(!paths::PicturesFolder().empty());
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

    TestPatternExpansion();
    TestPathJoining();
    TestUniquePath();

    TestGeometry();
    TestCrop();
    TestScale();

    TestHistory();
    TestShotLabels();

    TestDirectCaptureSequence();
    TestPermissionIsAskedBeforeHiding();
    TestRefusedPermissionEndsTheCapture();
    TestDelayCountdown();
    TestSecondRequestIsIgnored();
    TestRegionSequence();
    TestOverlayOnOneDisplayCropsTheBackdrop();
    TestTinySelectionIsNotAShot();
    TestCancelRestoresTheWindow();
    TestFailedGrabIsReportedAndRecovers();

    TestCaptureFromTheTrayStaysThere();
    TestCancelledCaptureFromTheTrayStaysThere();
    TestUnkeptShotBringsTheWindowBack();
    TestCaptureWithTheWindowUpShowsIt();

    TestRevisions();
    TestHotkeyRevisions();
    TestBarePrintScreen();
    TestPrintKeyGoesStaleWithTheHotkeys();
    TestRefreshHotkeysAsksAgainWithoutChangingAnything();
    TestPaths();

    return daveshot::testing::Summary("app");
}
