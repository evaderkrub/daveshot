#include "platform/Hotkeys.h"
#import <Carbon/Carbon.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace daveshot::hotkeys {
namespace {
std::vector<EventHotKeyRef> keys;
std::vector<Action> pending;
EventHandlerRef handler = nullptr;
std::string Normal(const std::string& name) {
    std::string s;
    for (unsigned char c : name) if (!std::isspace(c)) s += (char)std::tolower(c);
    return s;
}
bool Parse(const std::string& name, UInt32& key, UInt32& mods) {
    std::string s = Normal(name); mods = 0; key = 0;
    if (s == "none") return true;
    std::istringstream stream(s); std::string part;
    std::vector<std::string> parts;
    while (std::getline(stream, part, '+')) parts.push_back(part);
    if (parts.empty() || s.back() == '+') return false;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i] == "ctrl" || parts[i] == "control") mods |= controlKey;
        else if (parts[i] == "shift") mods |= shiftKey;
        else if (parts[i] == "alt" || parts[i] == "option") mods |= optionKey;
        else if (parts[i] == "cmd" || parts[i] == "command" || parts[i] == "super") mods |= cmdKey;
        else return false;
    }
    static const std::map<std::string, UInt32> codes = {
        {"a",kVK_ANSI_A},{"b",kVK_ANSI_B},{"c",kVK_ANSI_C},{"d",kVK_ANSI_D},{"e",kVK_ANSI_E},{"f",kVK_ANSI_F},
        {"g",kVK_ANSI_G},{"h",kVK_ANSI_H},{"i",kVK_ANSI_I},{"j",kVK_ANSI_J},{"k",kVK_ANSI_K},{"l",kVK_ANSI_L},
        {"m",kVK_ANSI_M},{"n",kVK_ANSI_N},{"o",kVK_ANSI_O},{"p",kVK_ANSI_P},{"q",kVK_ANSI_Q},{"r",kVK_ANSI_R},
        {"s",kVK_ANSI_S},{"t",kVK_ANSI_T},{"u",kVK_ANSI_U},{"v",kVK_ANSI_V},{"w",kVK_ANSI_W},{"x",kVK_ANSI_X},{"y",kVK_ANSI_Y},{"z",kVK_ANSI_Z},
        {"f1",kVK_F1},{"f2",kVK_F2},{"f3",kVK_F3},{"f4",kVK_F4},{"f5",kVK_F5},{"f6",kVK_F6},
        {"f7",kVK_F7},{"f8",kVK_F8},{"f9",kVK_F9},{"f10",kVK_F10},{"f11",kVK_F11},{"f12",kVK_F12}};
    auto found = codes.find(parts.back());
    if (found == codes.end()) return false;
    key = found->second; return true;
}
OSStatus OnHotkey(EventHandlerCallRef, EventRef event, void*) {
    EventHotKeyID id{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr || id.signature != 0x44534854) return eventNotHandledErr;
    pending.push_back((Action)id.id); return noErr;
}
}
const std::vector<std::string>& Presets() {
    static const std::vector<std::string> names = {"None", "Ctrl+Shift+S", "Ctrl+Shift+A", "Ctrl+Alt+S", "Cmd+Shift+S", "Cmd+Shift+A", "Alt+F9", "Ctrl+Shift+F9"};
    return names;
}
bool IsParseable(const std::string& name) { UInt32 key, mods; return Parse(name, key, mods); }
bool IsBarePrintScreen(const std::string& name) { const auto s = Normal(name); return s == "printscreen" || s == "print" || s == "prtsc"; }
void Install() {
    if (handler) return;
    EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(OnHotkey, 1, &type, nullptr, &handler);
}
void UnregisterAll() { for (auto key : keys) UnregisterEventHotKey(key); keys.clear(); pending.clear(); }
bool Register(const std::string& region, const std::string& screen, std::string& error) {
    UnregisterAll(); Install(); error.clear();
    auto add = [&](const std::string& name, Action action) {
        UInt32 key, mods;
        if (Normal(name) == "none") return;
        EventHotKeyRef ref = nullptr;
        if (!handler || !Parse(name, key, mods) || RegisterEventHotKey(key, mods, {0x44534854, (UInt32)action}, GetApplicationEventTarget(), 0, &ref) != noErr) {
            if (!error.empty()) error += "\n";
            error += "Could not register " + name + ". Choose another shortcut in Settings.";
        } else keys.push_back(ref);
    };
    add(region, Action_Region); add(screen, Action_Screen); return error.empty();
}
void Drain(std::vector<Action>& out) { out.swap(pending); pending.clear(); }
std::string TakeError() { return {}; }
void Shutdown() { UnregisterAll(); if (handler) { RemoveEventHandler(handler); handler = nullptr; } }
}
