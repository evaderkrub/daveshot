#include "app/Settings.h"

#include "platform/Paths.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace daveshot
{
namespace
{
    std::string Trim(const std::string& s)
    {
        size_t begin = 0;
        size_t end   = s.size();
        while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) ++begin;
        while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
        return s.substr(begin, end - begin);
    }

    bool ParseBool(const std::string& value, bool fallback)
    {
        if (value == "1" || value == "true"  || value == "yes") return true;
        if (value == "0" || value == "false" || value == "no")  return false;
        return fallback;
    }

    const char* BoolKey(bool value) { return value ? "true" : "false"; }
}

float ClampUiScale(float scale)
{
    // NaN fails every comparison, so test for the valid range rather than the
    // invalid one and let anything unordered fall through to the default.
    if (!(scale >= Settings::kMinScale && scale <= Settings::kMaxScale))
    {
        if (scale > Settings::kMaxScale) return Settings::kMaxScale;
        if (scale < Settings::kMinScale) return Settings::kMinScale;
        return 1.0f;
    }
    return scale;
}

int ClampDelaySeconds(int seconds)
{
    if (seconds < 0) return 0;
    if (seconds > Settings::kMaxDelaySeconds) return Settings::kMaxDelaySeconds;
    return seconds;
}

int ClampJpegQuality(int quality)
{
    if (quality < 1)   return 1;
    if (quality > 100) return 100;
    return quality;
}

int ClampHistoryLimit(int limit)
{
    if (limit < Settings::kMinHistory) return Settings::kMinHistory;
    if (limit > Settings::kMaxHistory) return Settings::kMaxHistory;
    return limit;
}

const char* FormatKey(ImageFormat format)
{
    return (format == ImageFormat::Jpeg) ? "jpeg" : "png";
}

ImageFormat FormatFromKey(const std::string& key)
{
    if (key == "jpeg" || key == "jpg")
        return ImageFormat::Jpeg;
    return ImageFormat::Png;
}

bool ParseSettings(const std::string& text, Settings& out)
{
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        const std::string key   = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));

        if      (key == "uiscale")        out.uiScale = ClampUiScale((float)std::atof(value.c_str()));
        else if (key == "theme" && !value.empty())           out.theme = value;
        else if (key == "savefolder")                        out.saveFolder = value;
        else if (key == "filenamepattern" && !value.empty()) out.filenamePattern = value;
        else if (key == "format")         out.format = FormatFromKey(value);
        else if (key == "jpegquality")    out.jpegQuality = ClampJpegQuality(std::atoi(value.c_str()));
        else if (key == "autosave")       out.autoSave = ParseBool(value, out.autoSave);
        else if (key == "autocopy")       out.autoCopy = ParseBool(value, out.autoCopy);
        else if (key == "delayseconds")   out.delaySeconds = ClampDelaySeconds(std::atoi(value.c_str()));
        else if (key == "hideoncapture")  out.hideOnCapture = ParseBool(value, out.hideOnCapture);
        else if (key == "hotkeyenabled")  out.hotkeyEnabled = ParseBool(value, out.hotkeyEnabled);
        else if (key == "hotkeyregion" && !value.empty())    out.hotkeyRegion = value;
        else if (key == "hotkeyscreen" && !value.empty())    out.hotkeyScreen = value;
        else if (key == "closetotray")    out.closeToTray = ParseBool(value, out.closeToTray);
        else if (key == "historylimit")   out.historyLimit = ClampHistoryLimit(std::atoi(value.c_str()));
    }
    return true;
}

std::string SerializeSettings(const Settings& s)
{
    char scale[64];
    std::snprintf(scale, sizeof(scale), "%.3f", (double)ClampUiScale(s.uiScale));

    std::string text = "# daveshot settings\n";
    text += "uiscale=";         text += scale;                    text += "\n";
    text += "theme=";           text += s.theme;                  text += "\n";
    text += "savefolder=";      text += s.saveFolder;             text += "\n";
    text += "filenamepattern="; text += s.filenamePattern;        text += "\n";
    text += "format=";          text += FormatKey(s.format);      text += "\n";
    text += "jpegquality=";     text += std::to_string(ClampJpegQuality(s.jpegQuality));    text += "\n";
    text += "autosave=";        text += BoolKey(s.autoSave);      text += "\n";
    text += "autocopy=";        text += BoolKey(s.autoCopy);      text += "\n";
    text += "delayseconds=";    text += std::to_string(ClampDelaySeconds(s.delaySeconds));  text += "\n";
    text += "hideoncapture=";   text += BoolKey(s.hideOnCapture); text += "\n";
    text += "hotkeyenabled=";   text += BoolKey(s.hotkeyEnabled); text += "\n";
    text += "hotkeyregion=";    text += s.hotkeyRegion;           text += "\n";
    text += "hotkeyscreen=";    text += s.hotkeyScreen;           text += "\n";
    text += "closetotray=";     text += BoolKey(s.closeToTray);   text += "\n";
    text += "historylimit=";    text += std::to_string(ClampHistoryLimit(s.historyLimit));  text += "\n";
    return text;
}

bool LoadSettings(const std::string& path, Settings& out, std::string& error)
{
    std::FILE* f = paths::OpenFile(path, "rb");
    if (f == nullptr)
        return true;   // first run: defaults stand

    std::string text;
    char chunk[1024];
    size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        text.append(chunk, read);

    const bool failed = std::ferror(f) != 0;
    std::fclose(f);

    if (failed)
    {
        error = "could not read " + path;
        return false;
    }
    return ParseSettings(text, out);
}

bool SaveSettings(const std::string& path, const Settings& settings, std::string& error)
{
    std::FILE* f = paths::OpenFile(path, "wb");
    if (f == nullptr)
    {
        error = "could not write " + path;
        return false;
    }

    const std::string text = SerializeSettings(settings);
    const size_t written = std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);

    if (written != text.size())
    {
        error = "could not write " + path;
        return false;
    }
    return true;
}
}
