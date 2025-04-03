#include "http.h"
#include "networking.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TIMEOUT 3000
#define MILLI_SEC 1000

// static const codeMapping code_map[] = {
//     {OK,              ""                                  },
//     {INVALID_USER_ID, "Invalid User ID"                   },
//     {INVALID_AUTH,    "Invalid Authentication Information"},
//     {USER_EXISTS,     "User Already exist"                },
//     {SERVER_ERROR,    "Server Error"                      },
//     {INVALID_REQUEST, "Invalid Request"                   },
//     {REQUEST_TIMEOUT, "Request Timeout"                   }
// };

// const char *code_to_string(const code_t *code)
// {
//     for(size_t i = 0; i < sizeof(code_map) / sizeof(code_map[0]); i++)
//     {
//         if(code_map[i].code == *code)
//         {
//             return code_map[i].msg;
//         }
//     }
//     return "UNKNOWN_STATUS";
// }

const struct fsm_transition transitions[] = {
    {START,            READ_REQUEST,     read_request    },
    {READ_REQUEST,     PARSER_REQUEST,   parse_request   },
    {PARSER_REQUEST,   PROCESS_REQUEST,  process_request },
    {PROCESS_REQUEST,  RESPONSE_HANDLER, response_handler},
    {RESPONSE_HANDLER, END,              NULL            },
    {ERROR_HANDLER,    END,              NULL            },
    {-1,               -1,               NULL            },
};

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

void fsm_run(int sockfd)
{
    request_t      request;
    fsm_state_func perform;
    fsm_state_t    from_id;
    fsm_state_t    to_id;

    from_id = START;
    to_id   = READ_REQUEST;

    memset(&request, 0, sizeof(request_t));

    request.raw = (char *)malloc(RAW_SIZE);
    if(!request.raw)
    {
        perror("failed to malloc");
        exit(EXIT_FAILURE);
    }
    memset(request.raw, 0, RAW_SIZE);
    request.sockfd = &sockfd;

    printf("%s\n", "workers spawned");

    // while(running)
    // {
    do
    {
        perform = fsm_transition(from_id, to_id, transitions);
        if(perform == NULL)
        {
            printf("illegal state %d, %d \n", from_id, to_id);
            goto cleanup;
        }
        // printf("from_id %d\n", from_id);
        from_id = to_id;
        to_id   = perform(&request);
        // printf("to_id %d\n", to_id);
    } while(to_id != END);
    // }

cleanup:
    free(request.raw);
}

fsm_state_t read_request(void *args)
{
    request_t *request    = (request_t *)args;
    size_t     bytes_read = 0;
    time_t     current;
    time_t     end;
    ssize_t    result;

    printf("%s\n", "in read_request");

    request->status = OK;

    request->client_fd = recv_fd(*request->sockfd, &request->fd_num);
    if(request->client_fd <= 0)
    {
        perror("recv_fd error");
    }
    printf("Worker %d (PID: %d) started\n", request->worker_id, getpid());

    printf("%s fd: %d num: %d\n", "receiving fd from monitor...", request->client_fd, request->fd_num);

    result = setSocketNonBlocking(request->client_fd, &request->err);
    if(result == -1)
    {
        free(request->raw);
        exit(EXIT_FAILURE);
    }

    current = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
    end     = current + TIMEOUT;

    while(bytes_read < RAW_SIZE && current <= end)
    {
        result  = read(request->client_fd, request->raw + bytes_read, RAW_SIZE - bytes_read);
        current = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
        if(result == 0)
        {
            return PARSER_REQUEST;
        }
        if(result == -1)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            request->err    = errno;
            request->status = INTERNAL_SERVER_ERROR;
            return ERROR_HANDLER;
        }
        bytes_read += (size_t)result;

        if(header_end(request->raw) > 0)
        {
            break;
        }
    }
    return PARSER_REQUEST;
}

fsm_state_t parse_request(void *args)
{
    request_t *request = (request_t *)args;

    request->status = OK;

    printf("%s\n", "in parse_request");

    // char buffer[MAX_LINE];
    // strncpy(buffer, request, MAX_LINE - 1);
    // buffer[MAX_LINE - 1] = '\0';  // Ensure null termination

    // char *line = strtok(buffer, "\r\n");

    // // Parse Request Line
    // if (line) {
    //     sscanf(line, "%7s %255s %15s", req->method, req->path, req->version);
    // }
    return PROCESS_REQUEST;
}

fsm_state_t process_request(void *args)
{
    request_t *request = (request_t *)args;

    request->status = OK;

    printf("%s\n", "in process_request");

    return RESPONSE_HANDLER;
}

fsm_state_t response_handler(void *args)
{
    request_t *request = (request_t *)args;

    request->status = OK;

    printf("%s\n", "in response_handler");

    return END;
}

fsm_state_t error_handler(void *args)
{
    request_t *request = (request_t *)args;

    request->status = OK;

    printf("%s\n", "in error_handler");

    return END;
}

// ssize_t write_fully(int fd, const void *buf, ssize_t size, int *err)
// {
//     size_t bytes_written = 0;
//     time_t current;
//     time_t end;

//     current = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
//     end     = current + TIMEOUT;

//     while(bytes_written < (size_t)size && current <= end)
//     {
//         ssize_t result = write(fd, (const char *)buf + bytes_written, (size_t)size - bytes_written);
//         current        = (time_t)(clock() * MILLI_SEC / CLOCKS_PER_SEC);
//         if(result == -1)
//         {
//             if(errno == EINTR || errno == EAGAIN)
//             {
//                 continue;
//             }
//             *err = errno;
//             return -1;
//         }
//         bytes_written += (size_t)result;
//     }
//     return (ssize_t)bytes_written;
// }

// ssize_t copy(int from, int to, int *err)
// {
//     char    buf[BUFFER_SIZE];
//     ssize_t nread;
//     ssize_t bytes_wrote;

//     memset(&buf, 0, BUFFER_SIZE);
//     do
//     {
//         errno = 0;
//         nread = read(from, buf, BUFFER_SIZE);
//         if(nread == 0)
//         {
//             return -1;
//         }
//         if(nread == 1 && buf[0] == '\0')
//         {
//             printf("server signal exit\n");
//             return -1;
//         }
//         if(nread < 0)
//         {
//             if(errno == EAGAIN)
//             {
//                 continue;
//             }
//             perror("read error\n");
//             goto error;
//         }

//         bytes_wrote = 0;
//         do
//         {
//             ssize_t twrote;
//             size_t  remaining;

//             remaining = (size_t)(nread - bytes_wrote);
//             errno     = 0;
//             twrote    = write(to, buf + bytes_wrote, remaining);
//             if(twrote < 0)
//             {
//                 if(errno == EAGAIN)
//                 {
//                     errno = 0;
//                     continue;
//                 }
//                 goto error;
//             }

//             bytes_wrote += twrote;
//         } while(bytes_wrote != nread);
//     } while(nread != 0);
//     return 0;

// error:
//     *err = errno;
//     return -1;
// }
