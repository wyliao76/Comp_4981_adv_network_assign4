#include "http.h"
#include "networking.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define TIMEOUT 3000
#define MILLI_SEC 1000

static const char *const Http_methods[]              = {"HEAD", "GET", "POST"};
static const char *const Unsupported_Http_methods[]  = {"PATCH", "PUT", "DELETE"};
static const char *const Http_versions[]             = {"HTTP/1.0", "HTTP/1.1"};
static const char *const Unsupported_Http_versions[] = {"HTTP/2.0", "HTTP/3.0"};
static const char *const default_index               = "/index.html";
static const char *const new_line                    = "\r\n";
static const char *const space                       = " ";
static const char *const terminate                   = "\r\n\r\n";
static const char *const content_type                = "Content-Type: ";
static const char *const content_len                 = "Content-Length: ";
static const char *const server                      = "Server: Tia\r\n";
static const char *const default_type                = "html";
static const char *const base_path                   = "./public";

static const char *mime_to_string(const char *mime);
static const char *status_to_string(status_t status);
static ssize_t     header_end(char *buf);
static ssize_t     check_method(request_t *request);
static ssize_t     check_HTTP(request_t *request);
static ssize_t     check_skipping(request_t *request);

static const MimeMapping Mime_map[] = {
    {"txt",  "text/plain\r\n"                   },
    {"html", "text/html\r\n"                    },
    {"css",  "text/css\r\n"                     },
    {"js",   "text/javascript\r\n"              },
    {"csv",  "text/csv\r\n"                     },
    {"jpeg", "image/jpeg\r\n"                   },
    {"jpg",  "image/jpeg\r\n"                   },
    {"png",  "image/png\r\n"                    },
    {"gif",  "image/gif\r\n"                    },
    {"json", "application/json\r\n"             },
    {"swf",  "application/x-shockwave-flash\r\n"}
};

static const char *mime_to_string(const char *mime)
{
    for(size_t i = 0; i < sizeof(Mime_map) / sizeof(Mime_map[0]); i++)
    {
        if(strcmp(Mime_map[i].mime, mime) == 0)
        {
            return Mime_map[i].name;
        }
    }
    return "text/plain\r\n";
}

static const StatusMapping status_map[] = {
    {OK,                    "200 OK\r\n"                   },
    {BAD_REQUEST,           "400 BAD REQUEST\r\n"          },
    {UNAUTHORIZED,          "401 UNAUTHORIZED\r\n"         },
    {FORBIDDEN,             "403 Forbidden\r\n"            },
    {NOT_FOUND,             "404 Not Found\r\n"            },
    {METHOD_NOT_ALLOWED,    "405 Method Not Allowed\r\n"   },
    {INTERNAL_SERVER_ERROR, "500 Internal Server Error\r\n"},
    {NOT_IMPLEMENTED,       "501 Not Implemented\r\n"      }
};

static const char *status_to_string(status_t status)
{
    for(size_t i = 0; i < sizeof(status_map) / sizeof(status_map[0]); i++)
    {
        if(status_map[i].status == status)
        {
            return status_map[i].name;
        }
    }
    return "UNKNOWN_STATUS";
}

static char *strcopy(char *to, const char *from, size_t len)
{
    do
    {
        *to++ = *from++;
    } while(--len);
    return to;
}

static char *strconcat(char *to, const char *from1, size_t len1, const char *from2, size_t len2)
{
    do
    {
        *to++ = *from1++;
    } while(--len1 != 0);

    do
    {
        *to++ = *from2++;
    } while(--len2 != 0);
    *to = '\0';
    return to;
}

static ssize_t head(request_t *request)
{
    return write_fully(request->client_fd, request->response, request->response_len, &request->err);
}

static ssize_t get(request_t *request)
{
    ssize_t result;

    result = write_fully(request->client_fd, request->response, request->response_len, &request->err);
    if(result == -1)
    {
        return result;
    }

    if(request->status == OK)
    {
        int input_fd;

        input_fd = open(request->path, O_RDONLY | O_CLOEXEC);
        if(input_fd < 0)
        {
            perror("open failed");
            request->err = errno;
            return -1;
        }

        result = copy(input_fd, request->client_fd, &request->err);

        close(input_fd);
    }
    return result;
}

// static ssize_t post(request_t *request)
// {

// }

static const funcMapping http_func[] = {
    {"HEAD", head},
    {"GET",  get },
 // {"POST",  post},
    {NULL,   NULL}  // Null termination for safety
};

static ssize_t execute_functions(request_t *request, const funcMapping functions[])
{
    if(request->status != OK)
    {
        return functions[0].func(request);
    }

    for(size_t i = 0; functions[i].method != NULL; i++)
    {
        if(strcmp(request->method, functions[i].method) == 0)
        {
            return functions[i].func(request);
        }
    }
    printf("Not builtin command: %s\n", request->method);
    request->status = NOT_IMPLEMENTED;
    return 1;
}

const struct fsm_transition transitions[] = {
    {START,            READ_REQUEST,     read_request    },
    {READ_REQUEST,     PARSER_REQUEST,   parse_request   },
    {PARSER_REQUEST,   CHECK_REQUEST,    check_request   },
    {CHECK_REQUEST,    RESPONSE_HANDLER, response_handler},
    {RESPONSE_HANDLER, END,              NULL            },
    {READ_REQUEST,     ERROR_HANDLER,    error_handler   },
    {PARSER_REQUEST,   ERROR_HANDLER,    error_handler   },
    {CHECK_REQUEST,    ERROR_HANDLER,    error_handler   },
    {RESPONSE_HANDLER, ERROR_HANDLER,    error_handler   },
    {ERROR_HANDLER,    END,              NULL            },
    {-1,               -1,               NULL            },
};

static ssize_t header_end(char *buf)
{
    const char *pos = strstr(buf, terminate);

    if(pos)
    {
        printf("Found at position: %td\n", pos - buf);
        return pos - buf;
    }
    printf("Not found\n");
    return -1;
}

static void trim_param(char *url)
{
    printf("trim_param: %s\n", url);

    do
    {
        if(*url == '?')
        {
            *url = '\0';
            break;
        }
    } while(*++url != '\0');

    while(*url++ != '\0')
    {
        *url = 0;
    }
}

static void url_decode(char *url)
{
    const int HEX = 16;
    char      output;

    while(*url)
    {
        if(*url == '%')
        {
            // Handle percent-encoded characters
            const char hex[3] = {url[1], url[2], '\0'};
            output            = (char)strtol(hex, NULL, HEX);    // Convert hex to char
            *url              = output;
            // 20in%20name.txt
            // in%20name.txt00
            strcopy(url + 1, url + 3, strlen(url + 1));
        }
        url++;
    }
}

static void get_timestamp(char *buffer, size_t size)
{
    time_t    t = time(NULL);
    struct tm buf;
    gmtime_r(&t, &buf);

    strftime(buffer, size, "Date: %a, %d %b %Y %H:%M:%S GMT", &buf);
}

static ssize_t check_method(request_t *request)
{
    for(size_t i = 0; i < sizeof(Http_methods) / sizeof(Http_methods[0]); ++i)
    {
        if(strcmp(request->method, Http_methods[i]) == 0)
        {
            return 0;
        }
    }

    for(size_t i = 0; i < sizeof(Unsupported_Http_methods) / sizeof(Unsupported_Http_methods[0]); ++i)
    {
        if(strcmp(request->method, Unsupported_Http_methods[i]) == 0)
        {
            request->status = METHOD_NOT_ALLOWED;
            return -2;
        }
    }
    request->status = INTERNAL_SERVER_ERROR;
    return -1;
}

static ssize_t check_HTTP(request_t *request)
{
    for(size_t i = 0; i < sizeof(Http_versions) / sizeof(Http_versions[0]); ++i)
    {
        if(strcmp(request->version, Http_versions[i]) == 0)
        {
            return 0;
        }
    }

    for(size_t i = 0; i < sizeof(Unsupported_Http_versions) / sizeof(Unsupported_Http_versions[0]); ++i)
    {
        if(strcmp(request->version, Unsupported_Http_versions[i]) == 0)
        {
            request->status = NOT_IMPLEMENTED;
            return 0;
        }
    }
    request->status = INTERNAL_SERVER_ERROR;
    return -1;
}

static ssize_t check_skipping(request_t *request)
{
    if(strstr(request->path, "/.."))
    {
        request->status = BAD_REQUEST;
        return -1;
    }
    return 0;
}

static ssize_t check_dir(request_t *request)
{
    struct stat file_stat;

    if(stat(request->path, &file_stat) == -1)
    {
        if(errno == ENOENT || errno == ENOTDIR)
        {
            errno           = 0;
            request->status = NOT_FOUND;
            return -1;
        }
        perror("check dir");
        request->err    = errno;
        request->status = INTERNAL_SERVER_ERROR;
        return -1;
    }
    if(S_ISDIR(file_stat.st_mode))
    {
        size_t path_size;

        errno     = 0;
        path_size = (size_t)strlen(request->path);

        if(*(request->path + path_size - 1) == '/')
        {
            --path_size;
        }

        strconcat(request->path, request->path, path_size, default_index, strlen(default_index));

        if(stat(request->path, &file_stat) == -1)
        {
            if(errno == ENOENT)
            {
                errno           = 0;
                request->status = FORBIDDEN;
                return -1;
            }
            perror("check dir is dir");
            request->status = INTERNAL_SERVER_ERROR;
            return -1;
        }
    }

    request->content_len        = file_stat.st_size;
    request->last_modified_time = file_stat.st_mtime;
    request->status             = OK;
    return 0;
}

static void parse_mime_type(request_t *request)
{
    size_t count;
    size_t index;
    char  *from;
    char  *to;

    from  = request->path;
    to    = request->mime_type;
    index = 0;
    count = 0;
    do
    {
        ++count;
        if(*from == '.')
        {
            index = count;
        }
    } while(*from++ != '\0');

    if(index == 0)
    {
        *to = '\0';
    }
    else
    {
        from = request->path + index;
        do
        {
            *to++ = *from++;
        } while(*from != '\0');
        *to = '\0';
    }

    printf("request->path: %s\n", request->path);
    printf("request->mime_type %s\n", request->mime_type);
}

void fsm_run(void *args)
{
    worker_t      *worker_args = (worker_t *)args;
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

    request.response = (char *)malloc(BUFFER_SIZE);
    if(!request.response)
    {
        perror("failed to malloc");
        free(request.raw);
        exit(EXIT_FAILURE);
    }
    memset(request.response, 0, BUFFER_SIZE);
    request.sockfd    = &worker_args->sockfd;
    request.client_fd = worker_args->client_fd;
    request.fd_num    = worker_args->fd_num;
    request.worker_id = &worker_args->worker_id;

    memcpy(request.mime_type, default_type, strlen(default_type));

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
    } while(to_id != END);

    printf("job done!\n");

cleanup:
    free(request.raw);
    free(request.response);
}

fsm_state_t read_request(void *args)
{
    request_t *request = (request_t *)args;
    ssize_t    result;

    printf("%s\n", "in read_request");

    request->status = OK;

    result = setSocketNonBlocking(request->client_fd, &request->err);
    if(result == -1)
    {
        return ERROR_HANDLER;
    }

    result = read_fully(request->client_fd, request->raw, RAW_SIZE, &request->err);
    if(result == -1)
    {
        printf("%s\n", "1");
        request->status = INTERNAL_SERVER_ERROR;
        return ERROR_HANDLER;
    }
    if(result == -2)
    {
        printf("%s\n", "2");
        request->status = BAD_REQUEST;
        return ERROR_HANDLER;
    }

    return PARSER_REQUEST;
}

fsm_state_t parse_request(void *args)
{
    request_t *request = (request_t *)args;

    char *line;
    char *saveptr;
    char *method;
    char *path;
    char *copy_path;
    char *version;

    printf("%s\n", "in parse_request");

    request->raw[RAW_SIZE - 1] = '\0';

    line = strtok_r(request->raw, new_line, &saveptr);

    if(!line)
    {
        return ERROR_HANDLER;
    }
    printf("Line: %s\n", line);

    method  = strtok_r(line, " ", &saveptr);
    path    = strtok_r(NULL, " ", &saveptr);
    version = strtok_r(NULL, " ", &saveptr);

    if(!method || !path || !version)
    {
        request->status = BAD_REQUEST;
        return ERROR_HANDLER;
    }

    request->method = method;
    printf("method: %s\n", request->method);

    copy_path = strndup(path, strlen(path));
    if(!copy_path)
    {
        request->status = INTERNAL_SERVER_ERROR;
        return ERROR_HANDLER;
    }

    trim_param(copy_path);
    url_decode(copy_path);

    memcpy(request->path, base_path, strlen(base_path));
    strncpy(request->path + strlen(base_path), copy_path, strlen(copy_path));

    free(copy_path);
    printf("path: %s\n", request->path);

    request->version = version;
    printf("version: %s\n", request->version);

    parse_mime_type(request);

    return CHECK_REQUEST;
}

fsm_state_t check_request(void *args)
{
    request_t *request = (request_t *)args;

    printf("%s\n", "in check_request");

    if(check_method(request) < 0 || check_HTTP(request) < 0 || check_skipping(request) < 0)
    {
        return ERROR_HANDLER;
    }

    if(check_dir(request) < 0)
    {
        return ERROR_HANDLER;
    }

    return RESPONSE_HANDLER;
}

static void process_request(void *args)
{
    request_t *request = (request_t *)args;

    char       *ptr;
    const char *status;
    const char *mime_type;
    char        size_buf[MIME_SIZE];
    char        timestamp[URL_SIZE];

    printf("%s\n", "in process_request");

    ptr    = request->response;
    ptr    = strcopy(ptr, Http_versions[0], strlen(Http_versions[0]));
    ptr    = strcopy(ptr, space, strlen(space));
    status = status_to_string(request->status);
    ptr    = strcopy(ptr, status, strlen(status));

    ptr = strcopy(ptr, server, strlen(server));

    get_timestamp(timestamp, URL_SIZE);
    ptr = strcopy(ptr, timestamp, strlen(timestamp));
    ptr = strcopy(ptr, new_line, strlen(new_line));

    mime_type = mime_to_string(request->mime_type);
    ptr       = strcopy(ptr, content_type, strlen(content_type));
    ptr       = strcopy(ptr, mime_type, strlen(mime_type));

    memset(size_buf, 0, MIME_SIZE);
    snprintf(size_buf, MIME_SIZE, "%lld", (long long)request->content_len);
    ptr    = strcopy(ptr, content_len, strlen(content_len));
    ptr    = strcopy(ptr, size_buf, strlen(size_buf));
    ptr    = strcopy(ptr, terminate, strlen(terminate));
    *++ptr = '\0';

    request->response_len = (ssize_t)strlen(request->response);
}

fsm_state_t response_handler(void *args)
{
    request_t *request = (request_t *)args;

    process_request(request);

    printf("%s\n", "in response_handler");

    printf("request->path: %s\n", request->path);
    printf("request->response: %s\n", request->response);

    if(execute_functions(request, http_func) == 1)
    {
        return ERROR_HANDLER;
    }

    close(request->client_fd);

    send_number(*request->sockfd, request->fd_num);
    printf("%s\n", "fd wrote back to server");
    printf("%s %d\n", "close fd worker side", request->client_fd);

    memset(request->raw, 0, RAW_SIZE);

    return END;
}

fsm_state_t error_handler(void *args)
{
    request_t *request = (request_t *)args;

    process_request(request);

    printf("%s\n", "in error_handler");

    execute_functions(request, http_func);

    close(request->client_fd);

    send_number(*request->sockfd, request->fd_num);
    printf("%s\n", "fd wrote back to server");
    printf("%s %d\n", "close fd worker side", request->client_fd);

    memset(request->raw, 0, RAW_SIZE);

    return END;
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
            return (ssize_t)bytes_read;
        }
    }
    return -2;
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
