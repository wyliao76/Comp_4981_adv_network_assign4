// cppcheck-suppress-file unusedStructMember

#ifndef HTTP_H
#define HTTP_H

#include "fsm.h"
#include <time.h>
#include <unistd.h>

#define RAW_SIZE 8192
#define BUFFER_SIZE 4096
#define METHOD_SIZE 8
#define PATH_SIZE 1024
#define VERSION_SIZE 16
#define MIME_SIZE 32
#define PARAMS 10

typedef enum
{
    READ_REQUEST = 2,
    PARSER_REQUEST,
    CHECK_REQUEST,
    RESPONSE_HANDLER,
    ERROR_HANDLER,
} fsm_state_http;

typedef enum
{
    OK                    = 200,
    BAD_REQUEST           = 400,
    UNAUTHORIZED          = 401,
    FORBIDDEN             = 403,
    NOT_FOUND             = 404,
    METHOD_NOT_ALLOWED    = 405,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED       = 501,
} status_t;

typedef struct param_t
{
    char *key;
    char *value;
} param_t;

typedef struct request_t
{
    char    *raw;
    char     method[METHOD_SIZE];
    char     path[PATH_SIZE];
    char     version[VERSION_SIZE];
    char     mime_type[MIME_SIZE];
    param_t *params[PARAMS];
    char    *response;
    ssize_t  response_len;
    off_t    content_len;
    time_t   last_modified_time;
    status_t status;
    int      client_fd;
    int      fd_num;
    int     *sockfd;
    int     *worker_id;
    int      err;
} request_t;

typedef struct
{
    status_t    status;
    const char *name;
} StatusMapping;

typedef struct
{
    const char *mime;
    const char *name;
} MimeMapping;

typedef struct funcMapping
{
    const char *method;
    ssize_t (*func)(request_t *request);
} funcMapping;

extern const struct fsm_transition transitions[];

void        fsm_run(void *args);
fsm_state_t read_request(void *args);
fsm_state_t parse_request(void *args);
fsm_state_t check_request(void *args);
fsm_state_t response_handler(void *args);
fsm_state_t error_handler(void *args);

ssize_t read_fully(int fd, char *buf, size_t size, int *err);
ssize_t write_fully(int fd, const void *buf, ssize_t size, int *err);
ssize_t copy(int from, int to, int *err);

#endif    // HTTP_H
