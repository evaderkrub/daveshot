#include "platform/linux/Dbus.h"

#include "platform/linux/Session.h"

#include <atomic>
#include <chrono>
#include <cstring>

namespace daveshot::dbus
{
namespace
{
    void AppendVariantString(DBusMessageIter& entry, const char* signature, int type,
                             const void* value)
    {
        DBusMessageIter variant;
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
        dbus_message_iter_append_basic(&variant, type, value);
        dbus_message_iter_close_container(&entry, &variant);
    }

    // Registers the application id on a freshly opened connection. It has
    // to be the first call on that connection -- the portal keys the app id
    // by the connection's unique name and refuses a late registration -- so
    // it lives in Open() rather than with the callers that benefit.
    void RegisterAppId(DBusConnection* connection)
    {
        DBusMessage* message = dbus_message_new_method_call(
            kPortalService, kPortalObject, "org.freedesktop.host.portal.Registry", "Register");
        if (message == nullptr)
            return;

        DBusMessageIter args;
        dbus_message_iter_init_append(message, &args);
        const char* appId = session::kAppId;
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &appId);
        AppendOptions(args, {});

        // An older portal without the registry answers with an error, and
        // that is fine: the permission just will not be remembered.
        DBusError err;
        dbus_error_init(&err);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message,
                                                                       2000, &err);
        if (reply != nullptr)
            dbus_message_unref(reply);
        dbus_error_free(&err);
        dbus_message_unref(message);
    }
}

void AppendOptions(DBusMessageIter& parent, const std::vector<Option>& options)
{
    DBusMessageIter dict;
    dbus_message_iter_open_container(&parent, DBUS_TYPE_ARRAY, "{sv}", &dict);
    for (const Option& option : options)
    {
        DBusMessageIter entry;
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char* key = option.key.c_str();
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

        switch (option.kind)
        {
        case Option::String:
        {
            const char* value = option.text.c_str();
            AppendVariantString(entry, "s", DBUS_TYPE_STRING, &value);
            break;
        }
        case Option::Bool:
        {
            const dbus_bool_t value = option.flag ? TRUE : FALSE;
            AppendVariantString(entry, "b", DBUS_TYPE_BOOLEAN, &value);
            break;
        }
        case Option::Uint32:
        {
            const dbus_uint32_t value = option.number;
            AppendVariantString(entry, "u", DBUS_TYPE_UINT32, &value);
            break;
        }
        }
        dbus_message_iter_close_container(&dict, &entry);
    }
    dbus_message_iter_close_container(&parent, &dict);
}

std::map<std::string, std::string> ReadStringDict(DBusMessageIter& dict,
                                                  std::vector<std::string>* keys)
{
    std::map<std::string, std::string> out;
    if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_ARRAY)
        return out;

    DBusMessageIter entries;
    dbus_message_iter_recurse(&dict, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&entries, &entry);

        const char* key = nullptr;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&entry, &key);
            if (keys != nullptr && key != nullptr)
                keys->push_back(key);
            dbus_message_iter_next(&entry);
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
            {
                DBusMessageIter variant;
                dbus_message_iter_recurse(&entry, &variant);
                const int type = dbus_message_iter_get_arg_type(&variant);
                if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH)
                {
                    const char* value = nullptr;
                    dbus_message_iter_get_basic(&variant, &value);
                    if (key && value)
                        out[key] = value;
                }
            }
        }
        dbus_message_iter_next(&entries);
    }
    return out;
}

Connection::~Connection()
{
    if (m_connection != nullptr)
    {
        dbus_connection_close(m_connection);
        dbus_connection_unref(m_connection);
        m_connection = nullptr;
    }
}

bool Connection::Open(std::string& error)
{
    if (m_connection != nullptr)
        return true;

    DBusError err;
    dbus_error_init(&err);
    m_connection = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (m_connection == nullptr)
    {
        error = std::string("no session bus: ") +
                (dbus_error_is_set(&err) ? err.message : "unknown reason");
        dbus_error_free(&err);
        return false;
    }
    dbus_error_free(&err);

    // Losing the bus should not take the process down with it; we would
    // rather report "the desktop is not answering" next time we are asked.
    dbus_connection_set_exit_on_disconnect(m_connection, FALSE);

    const char* unique = dbus_bus_get_unique_name(m_connection);
    m_senderToken.clear();
    if (unique != nullptr)
    {
        for (const char* p = unique; *p; ++p)
        {
            if (*p == ':') continue;
            m_senderToken += (*p == '.') ? '_' : *p;
        }
    }

    RegisterAppId(m_connection);
    return true;
}

void Connection::AddMatch(const std::string& rule)
{
    if (m_connection == nullptr)
        return;
    dbus_bus_add_match(m_connection, rule.c_str(), nullptr);
    dbus_connection_flush(m_connection);
}

bool Connection::Pump(int timeoutMs, const std::function<bool(DBusMessage*)>& onSignal)
{
    if (m_connection == nullptr)
        return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;)
    {
        // read_write_dispatch blocks up to the timeout for *one* message,
        // then pop_message drains whatever else arrived in the same read.
        const int wait = (timeoutMs <= 0) ? 0 : (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    deadline - std::chrono::steady_clock::now()).count();
        if (!dbus_connection_read_write(m_connection, wait < 0 ? 0 : wait))
            return false;   // disconnected

        for (;;)
        {
            DBusMessage* message = dbus_connection_pop_message(m_connection);
            if (message == nullptr)
                break;
            bool done = false;
            if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL)
                done = onSignal(message);
            dbus_message_unref(message);
            if (done)
                return true;
        }

        if (timeoutMs <= 0 || std::chrono::steady_clock::now() >= deadline)
            return false;
    }
}

DBusMessage* Connection::Call(DBusMessage* message, int timeoutMs, std::string& error)
{
    if (m_connection == nullptr)
    {
        error = "the session bus is not open";
        return nullptr;
    }

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(m_connection, message,
                                                                   timeoutMs, &err);
    if (reply == nullptr)
        error = dbus_error_is_set(&err) ? err.message : "no reply";
    dbus_error_free(&err);
    return reply;
}

Connection& Connection::Session()
{
    static Connection connection;
    return connection;
}

bool PermissionGranted(Connection& bus, const char* table, const char* id,
                       const std::string& appId)
{
    if (!bus.IsOpen())
        return false;

    DBusMessage* message = dbus_message_new_method_call(
        kPermissionStoreService, kPermissionStoreObject,
        kPermissionStoreService, "Lookup");
    if (message == nullptr)
        return false;

    DBusMessageIter args;
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &table);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &id);

    // A table nobody has written to yet answers with an error rather than an
    // empty dictionary, and an error means "not granted" just as plainly as
    // an absent entry does.
    std::string ignored;
    DBusMessage* reply = bus.Call(message, 2000, ignored);
    dbus_message_unref(message);
    if (reply == nullptr)
        return false;

    // (a{sas} permissions, v data): a map from application id to the list of
    // permissions the user granted it.
    bool granted = false;
    DBusMessageIter top;
    if (dbus_message_iter_init(reply, &top) &&
        dbus_message_iter_get_arg_type(&top) == DBUS_TYPE_ARRAY)
    {
        DBusMessageIter entries;
        dbus_message_iter_recurse(&top, &entries);
        while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&entries, &entry);

            const char* key = nullptr;
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&entry, &key);

            if (key != nullptr && appId == key && dbus_message_iter_next(&entry) &&
                dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY)
            {
                DBusMessageIter values;
                dbus_message_iter_recurse(&entry, &values);
                while (dbus_message_iter_get_arg_type(&values) == DBUS_TYPE_STRING)
                {
                    const char* value = nullptr;
                    dbus_message_iter_get_basic(&values, &value);
                    if (value != nullptr && std::strcmp(value, "yes") == 0)
                        granted = true;
                    dbus_message_iter_next(&values);
                }
                break;
            }
            dbus_message_iter_next(&entries);
        }
    }

    dbus_message_unref(reply);
    return granted;
}

std::string NewToken(const char* prefix)
{
    static std::atomic<unsigned> counter{ 0 };
    return std::string(prefix) + std::to_string(++counter);
}

bool StartPortalRequest(Connection& bus, const char* interface, const char* method,
                        const ArgumentWriter& writeArguments,
                        std::string& requestPath, std::string& error)
{
    if (!bus.Open(error))
        return false;

    const std::string token = NewToken("daveshot");
    requestPath = std::string(kPortalObject) + "/request/" + bus.SenderToken() + "/" + token;

    // Subscribe before calling: the portal may answer before the method
    // reply itself comes back, and a signal nobody was matching is dropped.
    bus.AddMatch("type='signal',interface='org.freedesktop.portal.Request',"
                 "member='Response',path='" + requestPath + "'");

    DBusMessage* message = dbus_message_new_method_call(kPortalService, kPortalObject,
                                                        interface, method);
    if (message == nullptr)
    {
        error = "could not build the portal request";
        return false;
    }

    DBusMessageIter args;
    dbus_message_iter_init_append(message, &args);
    writeArguments(args, token);

    DBusMessage* methodReply = bus.Call(message, 30000, error);
    dbus_message_unref(message);
    if (methodReply == nullptr)
    {
        error = std::string(interface) + "." + method + ": " + error;
        return false;
    }
    dbus_message_unref(methodReply);
    return true;
}

bool MatchResponse(DBusMessage* signal, const std::string& requestPath, PortalReply& reply)
{
    if (requestPath.empty() ||
        !dbus_message_is_signal(signal, "org.freedesktop.portal.Request", "Response"))
        return false;
    const char* path = dbus_message_get_path(signal);
    if (path == nullptr || requestPath != path)
        return false;

    DBusMessageIter iter;
    if (dbus_message_iter_init(signal, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_UINT32)
    {
        dbus_uint32_t code = 2;
        dbus_message_iter_get_basic(&iter, &code);
        reply.response = code;
        if (dbus_message_iter_next(&iter))
            reply.results = ReadStringDict(iter, &reply.keys);
    }
    return true;
}

bool PortalRequest(Connection& bus, const char* interface, const char* method,
                   const ArgumentWriter& writeArguments, int timeoutMs,
                   PortalReply& reply, std::string& error)
{
    std::string requestPath;
    if (!StartPortalRequest(bus, interface, method, writeArguments, requestPath, error))
        return false;

    bool got = false;
    bus.Pump(timeoutMs, [&](DBusMessage* signal)
    {
        got = MatchResponse(signal, requestPath, reply);
        return got;
    });

    if (!got)
    {
        error = std::string("the desktop did not answer the ") + method + " request";
        return false;
    }
    return true;
}
}
