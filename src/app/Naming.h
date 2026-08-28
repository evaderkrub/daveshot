#pragma once

#include <cstdint>
#include <string>

namespace daveshot
{
    // The pieces of a timestamp a filename pattern can refer to. Passed in
    // rather than read from the clock inside, so the expansion is a pure
    // function and the tests do not have to wait for a particular second.
    struct TimeParts
    {
        int year   = 1970;
        int month  = 1;      // 1-12
        int day    = 1;      // 1-31
        int hour   = 0;      // 0-23
        int minute = 0;
        int second = 0;
    };

    TimeParts LocalTimeNow();

    // Expands a filename pattern. Supported tokens, chosen to match strftime
    // so they are guessable:
    //
    //   %Y  year, 4 digits      %H  hour, 24-clock
    //   %m  month, 2 digits     %M  minute
    //   %d  day, 2 digits       %S  second
    //   %%  a literal percent
    //
    // Anything else after a % is left alone, percent and all, so a typo shows
    // up in the filename instead of silently disappearing.
    //
    // The result is sanitised: characters Windows forbids in a filename are
    // replaced with '-', leading and trailing dots and spaces are trimmed,
    // and an empty result becomes "daveshot". A pattern is a user-editable
    // string, and none of those cases should be able to produce a path the
    // save then fails on.
    std::string ExpandPattern(const std::string& pattern, const TimeParts& when);

    // Joins a folder and a filename with a single separator.
    std::string JoinPath(const std::string& folder, const std::string& name);

    // folder/name.ext, with " (2)", " (3)" and so on appended until the path
    // is free. Two captures inside the same second are the normal way to hit
    // this, and overwriting the first one silently would be the worst
    // possible answer.
    std::string UniquePath(const std::string& folder, const std::string& stem,
                           const std::string& extension);
}
