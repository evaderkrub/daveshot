#include "platform/SingleInstance.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace daveshot::instance
{
namespace
{
    int gListener = -1;

    // An abstract-namespace socket: a name in the kernel rather than a
    // file, so there is nothing to clean up after a crash and nothing to go
    // stale. The uid keeps one user's daveshot out of another's.
    sockaddr_un Address(socklen_t& length)
    {
        const std::string name = "org.daveshot.daveshot." + std::to_string((long)getuid());

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        address.sun_path[0] = '\0';
        std::strncpy(address.sun_path + 1, name.c_str(), sizeof(address.sun_path) - 2);
        length = (socklen_t)(offsetof(sockaddr_un, sun_path) + 1 + name.size());
        return address;
    }
}

bool Claim()
{
    socklen_t length = 0;
    const sockaddr_un address = Address(length);

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return true;   // could not ask; start anyway

    if (bind(fd, (const sockaddr*)&address, length) == 0)
    {
        if (listen(fd, 4) == 0)
        {
            gListener = fd;
            return true;
        }
        close(fd);
        return true;
    }

    if (errno != EADDRINUSE)
    {
        close(fd);
        return true;
    }
    close(fd);

    // Somebody else holds the name. Connecting is the doorbell: the other
    // side only needs to see that someone knocked.
    const int knock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (knock < 0)
        return false;
    const bool answered = connect(knock, (const sockaddr*)&address, length) == 0;
    if (answered)
    {
        const char show = 's';
        (void)!write(knock, &show, 1);
    }
    close(knock);

    // A name held by a process that no longer answers -- one being torn
    // down, say -- is as good as free.
    return !answered;
}

void Release()
{
    if (gListener >= 0)
    {
        close(gListener);
        gListener = -1;
    }
}

bool TakeWakeup()
{
    if (gListener < 0)
        return false;

    bool rang = false;
    for (;;)
    {
        const int caller = accept4(gListener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (caller < 0)
            break;
        close(caller);
        rang = true;
    }
    return rang;
}
}
