#include "platform/linux/HotkeyBackends.h"

#include "platform/linux/Dbus.h"
#include "platform/linux/Session.h"

// Global shortcuts through org.freedesktop.portal.GlobalShortcuts. The
// session is ours; the bindings are the desktop's. We say what we would
// like ("CTRL+SHIFT+s"), the user confirms or changes it in the desktop's
// own dialog, and from then on the desktop sends an Activated signal.
//
// Binding is a conversation, not a call: the answer can take as long as the
// user takes. So this runs as a small state machine advanced from Drain(),
// and the frame loop keeps drawing in the meantime.
//
// Every shortcut goes into one session and one BindShortcuts. That is not a
// tidiness preference: GNOME remembers a single set of shortcuts per
// application and each BindShortcuts replaces it, so a session per hotkey
// has each one evicting the other's binding. The store then never matches
// what is asked for, and the user is made to approve both keys again on
// every single launch. Asked for as one set, the set is remembered, and the
// desktop binds it silently from then on.

namespace daveshot::portalhotkeys
{
namespace
{
    constexpr const char* kInterface = "org.freedesktop.portal.GlobalShortcuts";

    enum class Stage
    {
        Idle,              // nothing registered
        CreatingSession,   // CreateSession sent, waiting for its Response
        Binding,           // BindShortcuts sent, the desktop may be asking the user
        Bound,             // Activated signals are live
        Failed,
    };

    std::vector<hotkeys::Binding> gBindings;
    Stage                         gStage = Stage::Idle;
    std::string                   gHandle;        // session object path
    std::string                   gRequestPath;   // the Request being waited on
    bool                          gMatchAdded = false;
    std::vector<hotkeys::Action>  gPending;
    std::string                   gError;

    const char* IdOf(hotkeys::Action action)
    {
        return (action == hotkeys::Action_Region) ? "region" : "screen";
    }

    const char* DescriptionOf(hotkeys::Action action)
    {
        return (action == hotkeys::Action_Region) ? "Capture a region of the screen"
                                                  : "Capture the whole screen";
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

    // What the whole set was asked to be, for a message about the whole set.
    std::string Describe()
    {
        std::string text;
        for (const hotkeys::Binding& binding : gBindings)
        {
            if (!text.empty()) text += ", ";
            text += std::string(binding.what) + " (" + TriggerOf(binding.combo) + ")";
        }
        return text;
    }

    void Close(dbus::Connection& bus)
    {
        if (!gHandle.empty())
        {
            DBusMessage* message = dbus_message_new_method_call(
                dbus::kPortalService, gHandle.c_str(),
                "org.freedesktop.portal.Session", "Close");
            if (message != nullptr)
            {
                std::string ignored;
                if (DBusMessage* reply = bus.Call(message, 2000, ignored))
                    dbus_message_unref(reply);
                dbus_message_unref(message);
            }
            gHandle.clear();
        }
        gRequestPath.clear();
    }

    bool StartCreate(dbus::Connection& bus, std::string& error)
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
            gRequestPath, error);
        gStage = started ? Stage::CreatingSession : Stage::Failed;
        return started;
    }

    bool StartBind(dbus::Connection& bus, std::string& error)
    {
        const bool started = dbus::StartPortalRequest(
            bus, kInterface, "BindShortcuts",
            [&](DBusMessageIter& args, const std::string& token)
            {
                const char* handle = gHandle.c_str();
                dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &handle);

                DBusMessageIter list;
                dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sa{sv})", &list);
                for (const hotkeys::Binding& binding : gBindings)
                {
                    DBusMessageIter entry;
                    dbus_message_iter_open_container(&list, DBUS_TYPE_STRUCT, nullptr, &entry);
                    const char* id = IdOf(binding.action);
                    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &id);
                    dbus::AppendOptions(entry, {
                        dbus::Option::Str("description", DescriptionOf(binding.action)),
                        dbus::Option::Str("preferred_trigger", TriggerOf(binding.combo)),
                    });
                    dbus_message_iter_close_container(&list, &entry);
                }
                dbus_message_iter_close_container(&args, &list);

                // Naming our window is what lets the desktop put its
                // shortcut dialog on screen at all -- see Session.h.
                const std::string window = session::ParentWindow();
                const char* parent = window.c_str();
                dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
                dbus::AppendOptions(args, { dbus::Option::Str("handle_token", token) });
            },
            gRequestPath, error);
        gStage = started ? Stage::Binding : Stage::Failed;
        return started;
    }

    void OnResponse(dbus::Connection& bus, const dbus::PortalReply& reply)
    {
        if (gStage == Stage::CreatingSession)
        {
            const auto handle = reply.results.find("session_handle");
            if (reply.response != 0 || handle == reply.results.end())
            {
                gError = "the desktop refused a shortcut session";
                gStage = Stage::Failed;
                Close(bus);
                return;
            }
            gHandle = handle->second;
            std::string error;
            if (!StartBind(bus, error))
            {
                gError = error;
                Close(bus);
            }
            return;
        }

        if (gStage == Stage::Binding)
        {
            gRequestPath.clear();
            // A reply that lists the bound shortcuts is a binding that stands,
            // whatever the code says: xdg-desktop-portal-gnome 48.0 grabs the
            // keys, names them back with the trigger it gave them, and then
            // answers 2. Taking that at face value would throw away a session
            // that works perfectly well.
            if (reply.response == 0 || reply.Has("shortcuts"))
            {
                gStage = Stage::Bound;
                return;
            }
            gError = (reply.response == 1)
                ? "the desktop's shortcut dialog was dismissed: " + Describe()
                : "the desktop would not bind " + Describe() +
                  " (another key is usually the fix)";
            gStage = Stage::Failed;
            Close(bus);
        }
    }

    // Activated(o session_handle, s shortcut_id, t timestamp, a{sv} options).
    // With every shortcut in the one session, the id is what says which key
    // was pressed.
    void OnActivated(DBusMessage* signal)
    {
        if (gStage != Stage::Bound)
            return;

        DBusMessageIter iter;
        if (!dbus_message_iter_init(signal, &iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
            return;
        const char* handle = nullptr;
        dbus_message_iter_get_basic(&iter, &handle);
        if (handle == nullptr || gHandle != handle)
            return;

        if (!dbus_message_iter_next(&iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
            return;
        const char* id = nullptr;
        dbus_message_iter_get_basic(&iter, &id);
        if (id == nullptr)
            return;

        for (const hotkeys::Binding& binding : gBindings)
            if (std::string(IdOf(binding.action)) == id)
            {
                gPending.push_back(binding.action);
                return;
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

    // A session's shortcuts are set once; changing them means a new session.
    Close(bus);
    gError.clear();
    gBindings = bindings;
    gStage    = Stage::Idle;
    if (gBindings.empty())
        return true;

    if (!gMatchAdded)
    {
        bus.AddMatch(std::string("type='signal',interface='") + kInterface +
                     "',member='Activated'");
        gMatchAdded = true;
    }

    std::string why;
    if (!StartCreate(bus, why))
        gError = why;
    return true;
}

void UnregisterAll()
{
    Close(dbus::Connection::Session());
    gBindings.clear();
    gStage = Stage::Idle;
}

void Drain(std::vector<hotkeys::Action>& out)
{
    out.clear();
    dbus::Connection& bus = dbus::Connection::Session();
    if (!bus.IsOpen() || gBindings.empty())
        return;

    bus.Pump(0, [&](DBusMessage* signal)
    {
        if (dbus_message_is_signal(signal, kInterface, "Activated"))
        {
            OnActivated(signal);
            return false;
        }
        dbus::PortalReply reply;
        if (dbus::MatchResponse(signal, gRequestPath, reply))
            OnResponse(bus, reply);
        return false;   // keep draining; more than one may be queued
    });

    out.swap(gPending);
    gPending.clear();
}

std::string TakeError()
{
    // Only once the desktop has answered, so the message is about a set that
    // is finished rather than one still being approved.
    if (gStage != Stage::Bound && gStage != Stage::Failed)
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
