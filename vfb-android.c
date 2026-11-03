/*
 * The screen of Xvfb in an AHardwareBuffer of the Android app (MIT, like the X server).
 *
 * The app makes the buffer and listens on the abstract unix socket that MEOWTTY_XBUF names. At the screen setup the
 * server connects, takes the buffer (AHardwareBuffer_recvHandleFromUnixSocket), locks it for the CPU and uses its memory
 * as the screen. The app shows the same buffer with no copy. The socket stays open: the app sees when the server ends.
 */
#include <android/hardware_buffer.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "vfb-android.h"

void *
vfbAndroidBuffer(int width, int height, int *stride_bytes)
{
    const char *name = getenv("MEOWTTY_XBUF");
    struct sockaddr_un addr;
    socklen_t len;
    AHardwareBuffer *buffer = NULL;
    AHardwareBuffer_Desc desc;
    void *pixels = NULL;
    int fd, tries;

    if (!name || !*name || strlen(name) > sizeof(addr.sun_path) - 2)
        return NULL;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, name, strlen(name)); /* abstract: the name starts after a 0 byte */
    len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return NULL;
    /* The app listens before it starts the server; wait a little in case it is slow. */
    for (tries = 0; connect(fd, (struct sockaddr *) &addr, len) != 0; tries++) {
        struct timespec wait = { 0, 100 * 1000 * 1000 };
        if (tries >= 50) {
            close(fd);
            return NULL;
        }
        nanosleep(&wait, NULL);
    }
    if (AHardwareBuffer_recvHandleFromUnixSocket(fd, &buffer) != 0 || !buffer) {
        close(fd);
        return NULL;
    }
    AHardwareBuffer_describe(buffer, &desc);
    if ((int) desc.width < width || (int) desc.height < height ||
        (desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM && desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM) ||
        AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                             -1, NULL, &pixels) != 0) {
        AHardwareBuffer_release(buffer);
        close(fd);
        return NULL;
    }
    /* The buffer and the socket live as long as the server. */
    *stride_bytes = (int) desc.stride * 4;
    return pixels;
}
