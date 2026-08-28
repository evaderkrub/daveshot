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

        if (key == "uiscale")
            out.uiScale = ClampUiScale((float)std::atof(value.c_str()));
        else if (key == "theme" && !value.empty())
            out.theme = value;
    }
    return true;
}

std::string SerializeSettings(const Settings& settings)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", (double)ClampUiScale(settings.uiScale));

    std::string text = "# daveshot settings\n";
    text += "uiscale=";
    text += buffer;
    text += "\ntheme=";
    text += settings.theme;
    text += "\n";
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
