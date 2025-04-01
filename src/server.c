#include "args.h"
#include "networking.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INADDRESS "0.0.0.0"
#define BACKLOG 5
#define PORT "8080"

int main(int argc, char *argv[], char *envp[])
{
    int         retval;
    args_t      args;
    int         err;
    const char *addr = getenv("ADDR");
    const char *port = getenv("PORT");
    int         sockfd[2];
    pid_t       pid;

    while(*envp)
    {
        PRINT_DEBUG("%s\n", *envp);
        envp++;
    }

    setup_signal();

    printf("Server launching... (press Ctrl+C to interrupt)\n");

    retval = EXIT_SUCCESS;
    err    = 0;

    memset(&args, 0, sizeof(args_t));
    args.addr = addr ? addr : INADDRESS;
    convert_port(port ? port : PORT, &args.port);
    verbose = convert(getenv("VERBOSE"));

    get_arguments(&args, argc, argv);

    printf("verbose: %d\n", verbose);
    printf("running: %d\n", running);
    PRINT_VERBOSE("%s\n", "verbose on");
    PRINT_DEBUG("%s\n", "debug on");

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockfd) == -1)
    {
        fprintf(stderr, "Error creating socket pair\n");
        retval = EXIT_FAILURE;
    }

    pid = fork();

    if(pid == -1)
    {
        fprintf(stderr, "Error creating child process\n");
        retval = EXIT_FAILURE;
    }

    if(pid == 0)
    {
        close(sockfd[1]);
        PRINT_VERBOSE("%s\n", "child process");
    }
    else
    {
        int server_fd;

        close(sockfd[0]);
        server_fd = tcp_server(args.addr, args.port, BACKLOG, &err);
        if(server_fd < 0)
        {
            fprintf(stderr, "main::tcp_server: Failed to create TCP server. %d\n", err);
            return EXIT_FAILURE;
        }

        printf("Listening on %s:%d\n", args.addr, args.port);
    }

    return retval;
}
