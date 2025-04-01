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

_Noreturn void usage(const char *binary_name, int exit_code, const char *message)
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
    exit(exit_code);
}

void get_arguments(args_t *args, int argc, char *argv[])
{
    int opt;

    static struct option long_options[] = {
        {"address", required_argument, NULL, 'a'},
        {"port",    required_argument, NULL, 'p'},
        {"verbose", optional_argument, NULL, 'v'},
        {"debug",   optional_argument, NULL, 'd'},
        {"help",    no_argument,       NULL, 'h'},
        {NULL,      0,                 NULL, 0  }
    };

    while((opt = getopt_long(argc, argv, "ha:p:A:P:vd", long_options, NULL)) != -1)
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

int convert(const char *str)
{
    char *endptr;
    long  sm_fd;

    errno = 0;
    if(!str)
    {
        return -1;
    }

    sm_fd = strtol(str, &endptr, BASE_TEN);

    // Check for conversion errors
    if((errno == ERANGE && (sm_fd == INT_MAX || sm_fd == INT_MIN)) || (errno != 0 && sm_fd > 2))
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

    printf("sm_fd: %ld\n", sm_fd);
    return (int)sm_fd;
}
