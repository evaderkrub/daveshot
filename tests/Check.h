#pragma once

// Minimal check macros for the console test binaries. A test framework would
// be another dependency to stage, and these binaries only need to say which
// expectation failed and return non-zero.

#include <cstdio>
#include <cstring>

namespace daveshot::testing
{
    inline int gFailures = 0;
    inline int gChecks   = 0;

    inline void Report(bool ok, const char* expression, const char* file, int line)
    {
        ++gChecks;
        if (ok)
            return;
        ++gFailures;
        std::printf("FAIL %s:%d  %s\n", file, line, expression);
    }

    inline int Summary(const char* suite)
    {
        std::printf("%s: %d checks, %d failed\n", suite, gChecks, gFailures);
        return (gFailures == 0) ? 0 : 1;
    }
}

#define CHECK(expr) ::daveshot::testing::Report((expr), #expr, __FILE__, __LINE__)

// Floating point comparisons in these tests are of values that were assigned,
// not computed, so an exact epsilon is not the point -- catching a wrong
// value is.
#define CHECK_NEAR(a, b) \
    ::daveshot::testing::Report(((a) - (b) < 0.0001f && (b) - (a) < 0.0001f), \
                                #a " ~= " #b, __FILE__, __LINE__)

#define CHECK_STR(a, b) \
    ::daveshot::testing::Report(std::strcmp((a), (b)) == 0, \
                                #a " == " #b, __FILE__, __LINE__)
