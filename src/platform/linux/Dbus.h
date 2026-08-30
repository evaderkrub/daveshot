#pragma once

#include <dbus/dbus.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// A thin edge over libdbus for the handful of calls the Linux backends make.
// Everything the desktop portal offers -- screenshots, global shortcuts, the
// file manager -- arrives over the session bus, and libdbus is the one client
// library that is on every machine with a bus at all.
namespace daveshot::dbus
{
    // One option in an a{sv} dictionary: the portal API takes all its
    // parameters this way.
    struct Option
    {
        enum Kind { String, Bool, Uint32 };

        std::string key;
        Kind        kind  = String;
        std::string text;
        bool        flag  = false;
        uint32_t    number = 0;

        static Option Str(std::string k, std::string v)
        {
            Option o; o.key = std::move(k); o.kind = String; o.text = std::move(v); return o;
        }
        static Option Flag(std::string k, bool v)
        {
            Option o; o.key = std::move(k); o.kind = Bool; o.flag = v; return o;
        }
        static Option U32(std::string k, uint32_t v)
        {
            Option o; o.key = std::move(k); o.kind = Uint32; o.number = v; return o;
        }
    };

    void AppendOptions(DBusMessageIter& parent, const std::vector<Option>& options);

    // The string-valued entries of an a{sv} at the iterator. Anything that is
    // not a string is skipped: the portal results we read are all strings or
    // object paths, and a full variant parser would be code with no caller.
    std::map<std::string, std::string> ReadStringDict(DBusMessageIter& dict,
                                                      std::vector<std::string>* keys = nullptr);

    // The process's one private connection to the session bus. Private
    // rather than the shared dbus_bus_get() one so its lifetime is ours and
    // so the app id registered on it (see Session.h) covers every portal
    // call the application makes.
    class Connection
    {
    public:
        ~Connection();

        // Opens the bus on first use. Returns false, with a reason, on a
        // machine with no session bus -- a bare X session over ssh, say.
        bool Open(std::string& error);
        bool IsOpen() const { return m_connection != nullptr; }

        DBusConnection* Raw() const { return m_connection; }

        // The portal names request objects after the caller's unique name
        // with the ':' dropped and '.' turned into '_'. Empty until Open().
        const std::string& SenderToken() const { return m_senderToken; }

        void AddMatch(const std::string& rule);

        // Handles queued and newly arrived messages. Every signal goes to the
        // callback; a callback returning true means "that was the one I was
        // waiting for" and stops the pump early. With timeoutMs of zero this
        // is a poll, which is what the frame loop wants.
        bool Pump(int timeoutMs, const std::function<bool(DBusMessage*)>& onSignal);

        // A plain method call with a reply, for calls that return directly
        // rather than through a Request object. Returns nullptr and sets
        // error on failure; the caller unrefs a non-null reply.
        DBusMessage* Call(DBusMessage* message, int timeoutMs, std::string& error);

        static Connection& Session();

    private:
        DBusConnection* m_connection = nullptr;
        std::string     m_senderToken;
    };

    // One portal request: calls a method on org.freedesktop.portal.Desktop
    // whose reply comes back later as a Response signal on a Request object.
    // Handles the handle_token dance and blocks until the response or the
    // timeout. `response` is the portal's code: 0 success, 1 cancelled by
    // the user, 2 failure.
    struct PortalReply
    {
        uint32_t                           response = 2;
        std::map<std::string, std::string> results;   // the string-valued entries
        std::vector<std::string>           keys;      // every entry, whatever its type

        bool Has(const char* key) const
        {
            for (const std::string& k : keys)
                if (k == key) return true;
            return false;
        }
    };

    using ArgumentWriter = std::function<void(DBusMessageIter& args, const std::string& handleToken)>;

    bool PortalRequest(Connection& bus, const char* interface, const char* method,
                       const ArgumentWriter& writeArguments, int timeoutMs,
                       PortalReply& reply, std::string& error);

    // The same request in two halves, for a caller that would rather keep
    // drawing frames than block: Start sends the call and hands back the
    // Request object's path; MatchResponse recognises that request's
    // Response signal when it turns up in a Pump callback.
    bool StartPortalRequest(Connection& bus, const char* interface, const char* method,
                            const ArgumentWriter& writeArguments,
                            std::string& requestPath, std::string& error);
    bool MatchResponse(DBusMessage* signal, const std::string& requestPath, PortalReply& reply);

    // Whether the desktop's permission store already has the user's answer
    // for one portal permission -- table "screenshot", id "screenshot" --
    // and that answer is yes. False when there is no entry, no store, or no
    // bus: in every one of those cases the portal is going to ask.
    //
    // Worth knowing in advance because the asking is what is awkward. The
    // desktop will only put the question on screen while our own window is
    // up, so the capture sequence has to find out before it hides.
    bool PermissionGranted(Connection& bus, const char* table, const char* id,
                           const std::string& appId);

    // A fresh handle_token: unique within the process, which is all the
    // portal asks.
    std::string NewToken(const char* prefix);

    inline constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
    inline constexpr const char* kPortalObject  = "/org/freedesktop/portal/desktop";

    inline constexpr const char* kPermissionStoreService =
        "org.freedesktop.impl.portal.PermissionStore";
    inline constexpr const char* kPermissionStoreObject =
        "/org/freedesktop/impl/portal/PermissionStore";
}
