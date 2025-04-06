#include "args.h"
#include "networking.h"
#include "utils.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNKNOWN_OPTION_MESSAGE_LEN 22
#define BASE_TEN 10
#define MAX_WORKERS 10
#define INADDRESS "0.0.0.0"
#define PORT "8080"
#define WORKERS 3

static _Noreturn void usage(const char *binary_name, int exit_code, const char *message);
static int            convert_str_t_l(const char *str);

static _Noreturn void usage(const char *binary_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] -a <address> -p <port>\n", binary_name);
    fputs("Options:\n", stderr);
    fputs("  -h, --help                         Display this help message\n", stderr);
    fputs("  -a <address>, --address <address>  The address of remote server.\n", stderr);
    fputs("  -p <port>,    --port <port>        The server port to use.\n", stderr);
    fputs("  -v <verbose>,    --verbose <verbose>        To show more logs.\n", stderr);
    fputs("  -d <debug>,    --debug <debug>        To show detail logs.\n", stderr);
    fputs("  -w <debug>,    --worker <worker>        worker number.\n", stderr);
    exit(exit_code);
}

void get_arguments(args_t *args, int argc, char *argv[])
{
    int opt;

    static struct option long_options[] = {
        {"address", optional_argument, NULL, 'a'},
        {"port",    optional_argument, NULL, 'p'},
        {"verbose", optional_argument, NULL, 'v'},
        {"debug",   optional_argument, NULL, 'd'},
        {"worker",  optional_argument, NULL, 'w'},
        {"help",    no_argument,       NULL, 'h'},
        {NULL,      0,                 NULL, 0  }
    };

    args->addr = getenv("ADDR") ? getenv("ADDR") : INADDRESS;
    convert_port(getenv("PORT") ? getenv("PORT") : PORT, &args->port);
    verbose       = convert_str_t_l(getenv("VERBOSE"));
    args->workers = convert_str_t_l(getenv("WORKERS")) != -1 ? convert_str_t_l(getenv("WORKERS")) : WORKERS;

    while((opt = getopt_long(argc, argv, "ha:p:A:P:w:vd", long_options, NULL)) != -1)
    {
        switch(opt)
        {
            case 'a':
                args->addr = optarg;
                break;
            case 'p':
                if(convert_port(optarg, &args->port) != 0)
                {
                    usage(argv[0], EXIT_FAILURE, "Port must be between 1 and 65535");
                }
                break;
            case 'v':
                verbose = 1;
                break;
            case 'd':
                verbose = 2;
                break;
            case 'w':
                args->workers = convert_str_t_l(optarg);
                if(args->workers > MAX_WORKERS || args->workers < 1)
                {
                    char msg[BUF_SIZE];
                    snprintf(msg, sizeof(msg), "Workers must be between 1 and %d", MAX_WORKERS);
                    usage(argv[0], EXIT_FAILURE, msg);
                }
                break;
            case 'h':
                usage(argv[0], EXIT_SUCCESS, NULL);
            case '?':
                if(optopt != 'a' && optopt != 'p')
                {
                    char message[UNKNOWN_OPTION_MESSAGE_LEN];

                    snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
                    usage(argv[0], EXIT_FAILURE, message);
                }
                break;
            default:
                usage(argv[0], EXIT_FAILURE, NULL);
        }
    }
}

int convert_str_t_l(const char *str)
{
    char *endptr;
    long  fd;

    errno = 0;
    if(!str)
    {
        return -1;
    }

    fd = strtol(str, &endptr, BASE_TEN);

    // Check for conversion errors
    if((errno == ERANGE && (fd == INT_MAX || fd == INT_MIN)) || (errno != 0 && fd > 2))
    {
        fprintf(stderr, "Error during conversion: %s\n", strerror(errno));
        return -1;
    }

    // Check if the entire string was converted
    if(endptr == str)
    {
        fprintf(stderr, "No digits were found in the input.\n");
        return -1;
    }

    // Check for leftover characters in the string
    if(*endptr != '\0')
    {
        fprintf(stderr, "Extra characters after the number: %s\n", endptr);
        return -1;
    }

    printf("fd: %ld\n", fd);
    return (int)fd;
}
