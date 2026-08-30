#include "platform/linux/ScreenBackends.h"

#include "platform/Paths.h"
#include "platform/linux/Dbus.h"
#include "platform/linux/ImageCodec.h"
#include "platform/linux/Session.h"

#include <cstdlib>
#include <cstring>

// Screen capture through org.freedesktop.portal.Screenshot. The portal
// takes the picture on our behalf -- a Wayland client cannot read the screen
// -- writes it to a file, and hands back the file's URI. The first time, the
// desktop asks the user whether daveshot may take screenshots; with the app
// id registered on the bus, the answer is remembered.
//
// That first time needs care. GNOME will only put the question on screen for
// an application it can point at: the request has to name our window, and
// our window has to be up. A screenshot tool hides itself before it shoots,
// so by the time we would naturally ask, both are gone -- the request is
// refused with a bare "failure", and no amount of retrying changes it,
// because the retry hides the window too. Hence NeedsPermission and
// RequestPermission below, which the capture sequence calls first, while
// there is still a window to hang the dialog on.

namespace daveshot::portalscreen
{
namespace
{
    std::string PathFromFileUri(const std::string& uri)
    {
        static const char* kPrefix = "file://";
        if (uri.rfind(kPrefix, 0) != 0)
            return std::string();

        std::string path;
        const std::string rest = uri.substr(std::strlen(kPrefix));
        for (size_t i = 0; i < rest.size(); ++i)
        {
            if (rest[i] == '%' && i + 2 < rest.size())
            {
                const std::string hex = rest.substr(i + 1, 2);
                path += (char)std::strtol(hex.c_str(), nullptr, 16);
                i += 2;
            }
            else
                path += rest[i];
        }
        return path;
    }

    // The portal's file is scratch: it was written for this one request,
    // we have the pixels now, and GNOME puts it straight into the user's
    // Pictures folder -- so leaving it would drop a stray "Screenshot-N.png"
    // there for every capture. The user's own save goes through Settings.
    void DiscardPortalFile(const std::string& path)
    {
        paths::RemoveFile(path);
    }

    // Set once the portal has actually taken a picture for us, which is the
    // only proof that the user's answer is on file.
    bool gPermitted = false;

    bool Screenshot(bool interactive, Image* out, std::string& error)
    {
        dbus::Connection& bus = dbus::Connection::Session();

        dbus::PortalReply reply;
        // The user is in the loop either way -- a permission prompt the first
        // time, the desktop's picker every time when interactive -- so the
        // wait is generous. A capture nobody responds to eventually gives up
        // rather than leaving the window hidden forever.
        const int timeoutMs = interactive ? 180000 : 90000;
        const bool called = dbus::PortalRequest(
            bus, "org.freedesktop.portal.Screenshot", "Screenshot",
            [&](DBusMessageIter& args, const std::string& token)
            {
                const std::string window = session::ParentWindow();
                const char* parent = window.c_str();
                dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
                dbus::AppendOptions(args, {
                    dbus::Option::Str("handle_token", token),
                    dbus::Option::Flag("interactive", interactive),
                    dbus::Option::Flag("modal", true),
                });
            },
            timeoutMs, reply, error);
        if (!called)
            return false;

        if (reply.response == 1)
        {
            error = interactive ? "the capture was cancelled"
                                : "screenshot permission was not granted";
            return false;
        }
        if (reply.response != 0)
        {
            error = "the desktop could not take a screenshot";
            return false;
        }
        gPermitted = true;

        const auto uri = reply.results.find("uri");
        if (uri == reply.results.end())
        {
            error = "the desktop returned no screenshot";
            return false;
        }
        const std::string path = PathFromFileUri(uri->second);
        if (path.empty())
        {
            error = "the desktop returned a screenshot we cannot read: " + uri->second;
            return false;
        }

        // A permission request wants nothing but the answer, and decoding a
        // whole-desktop PNG to throw it away is the most expensive way to
        // ignore something.
        bool decoded = true;
        if (out != nullptr)
            decoded = codec::DecodeFile(path, *out, error);
        DiscardPortalFile(path);
        return decoded;
    }
}

bool NeedsPermission()
{
    if (gPermitted || !session::IsWayland())
        return false;

    std::string ignored;
    dbus::Connection& bus = dbus::Connection::Session();
    if (!bus.Open(ignored))
        return false;   // no bus: the capture will fail, but not over this

    gPermitted = dbus::PermissionGranted(bus, "screenshot", "screenshot", session::kAppId);
    return !gPermitted;
}

bool RequestPermission(std::string& error)
{
    // There is no portal call that asks the question on its own, so the ask
    // is an ordinary screenshot whose picture we drop on the floor. It costs
    // one capture, once, on the first run after the application is installed.
    if (Screenshot(false, nullptr, error))
        return true;

    // Worth naming the permission: once the desktop has been told no, it
    // says no again on its own without ever showing a dialog, and "could not
    // take a screenshot" gives the user nothing to act on.
    error = "the desktop did not give daveshot permission to capture the "
            "screen (" + error + ")";
    return false;
}

bool CaptureDesktop(Image& out, std::string& error)
{
    if (!Screenshot(false, &out, error))
        return false;
    linuxscreen::CalibrateCaptureScale(out.width, out.height);
    return true;
}

bool CaptureInteractive(Image& out, std::string& error)
{
    return Screenshot(true, &out, error);
}
}
