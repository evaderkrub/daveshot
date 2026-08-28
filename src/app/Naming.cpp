#include "app/Naming.h"

#include "platform/Paths.h"

#include <cstdio>
#include <ctime>

namespace daveshot
{
namespace
{
    void AppendPadded(std::string& out, int value, int width)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%0*d", width, value);
        out += buffer;
    }

    bool IsForbidden(char c)
    {
        // The set Windows rejects, plus control characters. '/' is in here
        // too: a pattern is a filename, not a path, and letting it introduce
        // directory separators would put captures somewhere the user did not
        // ask for.
        switch (c)
        {
        case '<': case '>': case ':': case '"':
        case '/': case '\\': case '|': case '?': case '*':
            return true;
        default:
            return (unsigned char)c < 0x20;
        }
    }

    std::string Sanitise(std::string name)
    {
        for (char& c : name)
            if (IsForbidden(c))
                c = '-';

        size_t begin = 0;
        size_t end   = name.size();
        while (begin < end && (name[begin] == ' ' || name[begin] == '.')) ++begin;
        while (end > begin && (name[end - 1] == ' ' || name[end - 1] == '.')) --end;
        name = name.substr(begin, end - begin);

        if (name.empty())
            name = "daveshot";
        return name;
    }
}

TimeParts LocalTimeNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    TimeParts parts;
    parts.year   = local.tm_year + 1900;
    parts.month  = local.tm_mon + 1;
    parts.day    = local.tm_mday;
    parts.hour   = local.tm_hour;
    parts.minute = local.tm_min;
    parts.second = local.tm_sec;
    return parts;
}

std::string ExpandPattern(const std::string& pattern, const TimeParts& when)
{
    std::string out;
    out.reserve(pattern.size() + 16);

    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i] != '%' || i + 1 >= pattern.size())
        {
            out += pattern[i];
            continue;
        }

        const char token = pattern[i + 1];
        ++i;
        switch (token)
        {
        case 'Y': AppendPadded(out, when.year,   4); break;
        case 'm': AppendPadded(out, when.month,  2); break;
        case 'd': AppendPadded(out, when.day,    2); break;
        case 'H': AppendPadded(out, when.hour,   2); break;
        case 'M': AppendPadded(out, when.minute, 2); break;
        case 'S': AppendPadded(out, when.second, 2); break;
        case '%': out += '%'; break;
        default:
            out += '%';
            out += token;
            break;
        }
    }

    return Sanitise(out);
}

std::string JoinPath(const std::string& folder, const std::string& name)
{
    if (folder.empty())
        return name;

    const char last = folder.back();
    if (last == '/' || last == '\\')
        return folder + name;
    return folder + "/" + name;
}

std::string UniquePath(const std::string& folder, const std::string& stem,
                       const std::string& extension)
{
    std::string candidate = JoinPath(folder, stem + "." + extension);
    if (!paths::Exists(candidate))
        return candidate;

    // Bounded so a folder the process cannot write to -- where Exists keeps
    // saying "taken" for a reason that has nothing to do with the name --
    // fails at the save with a real message instead of spinning here.
    for (int suffix = 2; suffix < 1000; ++suffix)
    {
        candidate = JoinPath(folder, stem + " (" + std::to_string(suffix) + ")." + extension);
        if (!paths::Exists(candidate))
            return candidate;
    }
    return candidate;
}
}
