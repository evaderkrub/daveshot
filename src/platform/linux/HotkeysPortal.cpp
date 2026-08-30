#include "platform/linux/HotkeyBackends.h"

#include "platform/linux/Dbus.h"
#include "platform/linux/Session.h"

// Global shortcuts through org.freedesktop.portal.GlobalShortcuts. The
// session is ours; the bindings are the desktop's. We say what we would
// like ("CTRL+SHIFT+s"), the user confirms or changes it in the desktop's
// own dialog, and from then on the desktop sends an Activated signal.
//
// Binding is a conversation, not a call: GNOME puts up two dialogs before
// it answers, and the answer can take as long as the user takes. So this
// runs as a small state machine advanced from Drain(), and the frame loop
// keeps drawing in the meantime.
//
// Each hotkey gets a session of its own. A session's shortcuts are bound as
// one set, and the desktop refuses the whole set if the shell already holds
// any key in it -- so with one session, PrintScreen being GNOME's would take
// the region hotkey down with it. Sessions are bound one after another, so
// the user sees one dialog at a time.

namespace daveshot::portalhotkeys
{
namespace
{
    constexpr const char* kInterface = "org.freedesktop.portal.GlobalShortcuts";

    enum class Stage
    {
        Waiting,           // its turn has not come
        CreatingSession,   // CreateSession sent, waiting for its Response
        Binding,           // BindShortcuts sent, the desktop is asking the user
        Bound,             // Activated signals are live
        Failed,
    };

    struct Session
    {
        hotkeys::Binding binding;
        Stage            stage = Stage::Waiting;
        std::string      handle;        // object path; empty until created
        std::string      requestPath;   // the Request being waited on
    };

    std::vector<Session>         gSessions;
    bool                         gMatchAdded = false;
    std::vector<hotkeys::Action> gPending;
    std::string                  gError;

    const char* IdOf(hotkeys::Action action)
    {
        return (action == hotkeys::Action_Region) ? "region" : "screen";
    }

    // The shortcut spec's spelling: upper-case modifier names, then an xkb
    // keysym name -- lower-case letters, "Print", "F9", "space".
    std::string TriggerOf(const hotkeys::KeyCombo& combo)
    {
        std::string trigger;
        if (combo.ctrl)  trigger += "CTRL+";
        if (combo.shift) trigger += "SHIFT+";
        if (combo.alt)   trigger += "ALT+";
        if (combo.super) trigger += "LOGO+";

        std::string key = combo.key;
        if (key == "print")       key = "Print";
        else if (key == "insert") key = "Insert";
        else if (key == "home")   key = "Home";
        else if (key.size() >= 2 && key[0] == 'f')
            key[0] = 'F';
        return trigger + key;
    }

    void AppendFailure(const std::string& what)
    {
        if (!gError.empty()) gError += "; ";
        gError += what;
    }

    void Close(dbus::Connection& bus, Session& session)
    {
        if (!session.handle.empty())
        {
            DBusMessage* message = dbus_message_new_method_call(
                dbus::kPortalService, session.handle.c_str(),
                "org.freedesktop.portal.Session", "Close");
            if (message != nullptr)
            {
                std::string ignored;
                if (DBusMessage* reply = bus.Call(message, 2000, ignored))
                    dbus_message_unref(reply);
                dbus_message_unref(message);
            }
            session.handle.clear();
        }
        session.requestPath.clear();
    }

    void CloseAll(dbus::Connection& bus)
    {
        for (Session& session : gSessions)
            Close(bus, session);
        gSessions.clear();
    }

    bool StartCreate(dbus::Connection& bus, Session& session, std::string& error)
    {
        const bool started = dbus::StartPortalRequest(
            bus, kInterface, "CreateSession",
            [&](DBusMessageIter& args, const std::string& token)
            {
                dbus::AppendOptions(args, {
                    dbus::Option::Str("handle_token", token),
                    dbus::Option::Str("session_handle_token", dbus::NewToken("daveshot_session")),
                });
            },
            session.requestPath, error);
        session.stage = started ? Stage::CreatingSession : Stage::Failed;
        return started;
    }

    bool StartBind(dbus::Connection& bus, Session& session, std::string& error)
    {
        const bool started = dbus::StartPortalRequest(
            bus, kInterface, "BindShortcuts",
            [&](DBusMessageIter& args, const std::string& token)
            {
                const char* handle = session.handle.c_str();
                dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &handle);

                DBusMessageIter list;
                dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sa{sv})", &list);
                DBusMessageIter entry;
                dbus_message_iter_open_container(&list, DBUS_TYPE_STRUCT, nullptr, &entry);
                const char* id = IdOf(session.binding.action);
                dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &id);
                dbus::AppendOptions(entry, {
                    dbus::Option::Str("description",
                                      session.binding.action == hotkeys::Action_Region
                                          ? "Capture a region of the screen"
                                          : "Capture the whole screen"),
                    dbus::Option::Str("preferred_trigger", TriggerOf(session.binding.combo)),
                });
                dbus_message_iter_close_container(&list, &entry);
                dbus_message_iter_close_container(&args, &list);

                // Naming our window is what lets the desktop put its
                // shortcut dialog on screen at all -- see Session.h.
                // Qualified from the root: `session` is also the local
                // binding being registered, two lines down.
                const std::string window = daveshot::session::ParentWindow();
                const char* parent = window.c_str();
                dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
                dbus::AppendOptions(args, { dbus::Option::Str("handle_token", token) });
            },
            session.requestPath, error);
        session.stage = started ? Stage::Binding : Stage::Failed;
        return started;
    }

    // Kicks off the next session that has not had its turn. One at a time:
    // the desktop's dialogs would otherwise stack up.
    void Advance(dbus::Connection& bus)
    {
        for (const Session& session : gSessions)
            if (session.stage == Stage::CreatingSession || session.stage == Stage::Binding)
                return;   // one is in flight

        for (Session& session : gSessions)
        {
            if (session.stage != Stage::Waiting)
                continue;
            std::string error;
            if (!StartCreate(bus, session, error))
                AppendFailure(std::string(session.binding.what) + ": " + error);
            else
                return;
        }
    }

    void OnResponse(dbus::Connection& bus, Session& session, const dbus::PortalReply& reply)
    {
        if (session.stage == Stage::CreatingSession)
        {
            const auto handle = reply.results.find("session_handle");
            if (reply.response != 0 || handle == reply.results.end())
            {
                AppendFailure(std::string(session.binding.what) +
                              ": the desktop refused a shortcut session");
                session.stage = Stage::Failed;
                Close(bus, session);
                return;
            }
            session.handle = handle->second;
            std::string error;
            if (!StartBind(bus, session, error))
            {
                AppendFailure(std::string(session.binding.what) + ": " + error);
                Close(bus, session);
            }
            return;
        }

        if (session.stage == Stage::Binding)
        {
            session.requestPath.clear();
            // A reply that lists the bound shortcuts is a binding that stands,
            // whatever the code says: xdg-desktop-portal-gnome 48.0 grabs the
            // key, announces it, and then answers 2. Taking that at face
            // value would close the one session that works.
            if (reply.response == 0 || reply.Has("shortcuts"))
            {
                session.stage = Stage::Bound;
                return;
            }
            const std::string why = (reply.response == 1)
                ? "the desktop's shortcut dialog was dismissed"
                : "the desktop would not bind it (another key is usually the fix)";
            AppendFailure(std::string(session.binding.what) + " (" +
                          TriggerOf(session.binding.combo) + "): " + why);
            session.stage = Stage::Failed;
            Close(bus, session);
        }
    }

    void OnActivated(DBusMessage* signal)
    {
        DBusMessageIter iter;
        if (!dbus_message_iter_init(signal, &iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
            return;
        const char* handle = nullptr;
        dbus_message_iter_get_basic(&iter, &handle);
        if (handle == nullptr)
            return;

        for (const Session& session : gSessions)
        {
            if (session.stage == Stage::Bound && session.handle == handle)
            {
                gPending.push_back(session.binding.action);
                return;
            }
        }
    }
}

bool Register(const std::vector<hotkeys::Binding>& bindings, std::string& error)
{
    dbus::Connection& bus = dbus::Connection::Session();
    if (!bus.Open(error))
    {
        error = "global hotkeys need the desktop portal: " + error;
        return false;
    }

    // A session's bindings are set once; changing them means new sessions.
    CloseAll(bus);
    gError.clear();
    for (const hotkeys::Binding& binding : bindings)
    {
        Session session;
        session.binding = binding;
        gSessions.push_back(session);
    }
    if (gSessions.empty())
        return true;

    if (!gMatchAdded)
    {
        bus.AddMatch(std::string("type='signal',interface='") + kInterface +
                     "',member='Activated'");
        gMatchAdded = true;
    }
    Advance(bus);
    return true;
}

void UnregisterAll()
{
    CloseAll(dbus::Connection::Session());
}

void Drain(std::vector<hotkeys::Action>& out)
{
    out.clear();
    dbus::Connection& bus = dbus::Connection::Session();
    if (!bus.IsOpen() || gSessions.empty())
        return;

    bool advanced = false;
    bus.Pump(0, [&](DBusMessage* signal)
    {
        if (dbus_message_is_signal(signal, kInterface, "Activated"))
        {
            OnActivated(signal);
            return false;
        }
        for (Session& session : gSessions)
        {
            dbus::PortalReply reply;
            if (dbus::MatchResponse(signal, session.requestPath, reply))
            {
                OnResponse(bus, session, reply);
                advanced = true;
                break;
            }
        }
        return false;   // keep draining; more than one may be queued
    });

    if (advanced)
        Advance(bus);

    out.swap(gPending);
    gPending.clear();
}

std::string TakeError()
{
    // Only once every session has had its answer, so one message covers all
    // of them rather than a modal per key.
    for (const Session& session : gSessions)
        if (session.stage != Stage::Bound && session.stage != Stage::Failed)
            return std::string();
    std::string error;
    error.swap(gError);
    return error;
}

void Shutdown()
{
    UnregisterAll();
    gPending.clear();
    gError.clear();
}
}
