#include "io.h"
#include <errno.h>
#include <fcntl.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <string.h>
#include <unistd.h>

ssize_t read_fully(int fd, char *buf, size_t size, int *err)
{
    size_t bytes_read = 0;
    while(bytes_read < size)
    {
        ssize_t result = read(fd, buf + bytes_read, size - bytes_read);
        if(result == 0)
        {
            break;    // EOF reached
        }
        if(result == -1)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;    // Interrupted, retry
            }
            *err = errno;
            return -1;    // Error occurred
        }
        bytes_read += (size_t)result;
    }
    return (ssize_t)bytes_read;
}

ssize_t write_fully(int fd, const void *buf, ssize_t size, int *err)
{
    size_t bytes_written = 0;
    while(bytes_written < (size_t)size)
    {
        ssize_t result = write(fd, (const char *)buf + bytes_written, (size_t)size - bytes_written);
        if(result == -1)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;    // Interrupted, retry
            }
            *err = errno;
            return -1;    // Error occurred
        }
        bytes_written += (size_t)result;
    }
    return (ssize_t)bytes_written;
}

ssize_t copy(int from, int to, int *err)
{
    char    buf[BUFFER_SIZE];
    ssize_t nread;
    ssize_t bytes_wrote;

    memset(&buf, 0, BUFFER_SIZE);
    do
    {
        errno = 0;
        nread = read(from, buf, BUFFER_SIZE);
        if(nread == 0)
        {
            return -1;
        }
        if(nread == 1 && buf[0] == '\0')
        {
            printf("server signal exit\n");
            return -1;
        }
        if(nread < 0)
        {
            if(errno == EAGAIN)
            {
                continue;
            }
            perror("read error\n");
            goto error;
        }

        bytes_wrote = 0;
        do
        {
            ssize_t twrote;
            size_t  remaining;

            remaining = (size_t)(nread - bytes_wrote);
            errno     = 0;
            twrote    = write(to, buf + bytes_wrote, remaining);
            if(twrote < 0)
            {
                if(errno == EAGAIN)
                {
                    errno = 0;
                    continue;
                }
                goto error;
            }

            bytes_wrote += twrote;
        } while(bytes_wrote != nread);
    } while(nread != 0);
    return 0;

error:
    *err = errno;
    return -1;
}
