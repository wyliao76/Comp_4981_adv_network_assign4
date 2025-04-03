// cppcheck-suppress-file unusedStructMember

#ifndef HTTP_H
#define HTTP_H

#include "fsm.h"

#define RAW_SIZE 8192

typedef enum
{
    READ_REQUEST = 2,
    PARSER_REQUEST,
    PROCESS_REQUEST,
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
    Not_Implemented       = 501,
} status_t;

typedef struct request_t
{
    char    *raw;
    char    *method;
    char    *path;
    char    *version;
    status_t status;
    int      client_fd;
    int      fd_num;
    int     *sockfd;
    int      worker_id;
    int      err;
} request_t;

extern const struct fsm_transition transitions[];

void        fsm_run(int sockfd);
fsm_state_t read_request(void *args);
fsm_state_t parse_request(void *args);
fsm_state_t process_request(void *args);
fsm_state_t response_handler(void *args);
fsm_state_t error_handler(void *args);

#endif    // HTTP_H
