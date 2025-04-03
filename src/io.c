#include "io.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TIMEOUT 5000
#define MILLI_SEC 1000

static long header_end(char *buf)
{
    const char *pos = strstr(buf, "\r\n\r\n");

    if(pos)
    {
        printf("Found at position: %td\n", pos - buf);
        return pos - buf;
    }
    printf("Not found\n");
    return -1;
}

ssize_t read_fully(int fd, char *buf, size_t size, int *err)
{
    size_t bytes_read = 0;
    time_t current;
    time_t end;

    current = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
    end     = current + TIMEOUT;

    while(bytes_read < size && current <= end)
    {
        ssize_t result = read(fd, buf + bytes_read, size - bytes_read);
        current        = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
        if(result == 0)
        {
            break;    // EOF reached
        }
        if(result == -1)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            *err = errno;
            return -1;
        }
        bytes_read += (size_t)result;
        if(header_end(buf) > 0)
        {
            break;
        }
    }
    return (ssize_t)bytes_read;
}

ssize_t write_fully(int fd, const void *buf, ssize_t size, int *err)
{
    size_t bytes_written = 0;
    time_t current;
    time_t end;

    current = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
    end     = current + TIMEOUT;

    while(bytes_written < (size_t)size && current <= end)
    {
        ssize_t result = write(fd, (const char *)buf + bytes_written, (size_t)size - bytes_written);
        current        = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
        if(result == -1)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            *err = errno;
            return -1;
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
