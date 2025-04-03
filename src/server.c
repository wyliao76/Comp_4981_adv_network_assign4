#include "args.h"
#include "fsm.h"
#include "io.h"
#include "networking.h"
#include "utils.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define BACKLOG 5
#define MAX_CLIENTS 3
#define MAX_FDS (MAX_CLIENTS + 2)
#define NUM_WORKERS 2
#define RAW_SIZE 8192

typedef struct request_t
{
    char *raw;
    int   client_fd;
    int   fd_num;
    int   sockfd;
    int   worker_id;
    int   err;
} request_t;

static void worker_process(void *args)
{
    request_t *request = (request_t *)args;

    const char *http_response = "HTTP/1.0 200 OK\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 13\r\n"
                                "Connection: close\r\n"
                                "\r\n"
                                "Hello, World!";

    PRINT_VERBOSE("Worker %d (PID: %d) started\n", request->worker_id, getpid());

    if(setSocketNonBlocking(request->client_fd, &request->err) < 0)
    {
        perror("set non-blocking failed");
    }

    read_fully(request->client_fd, request->raw, RAW_SIZE, &request->err);

    PRINT_VERBOSE("Worker %d is handling tasks...\n", request->worker_id);
    write_fully(request->client_fd, http_response, (ssize_t)strlen(http_response), &request->err);

    close(request->client_fd);

    send_number(request->sockfd, request->fd_num);
    PRINT_VERBOSE("%s\n", "fd wrote back to server");
    PRINT_VERBOSE("%s %d\n", "close fd worker side", request->client_fd);

    memset(request->raw, 0, RAW_SIZE);
}

static fsm_state_t event_loop(void *args);

static fsm_state_t event_loop(void *args)
{
    args_t       *server_args = (args_t *)args;
    struct pollfd fds[MAX_FDS];
    int           client_fd;
    int           added;

    PRINT_DEBUG("%s%d\n", "event loop: ", running);

    fds[0].fd     = *server_args->fd;
    fds[0].events = POLLIN;

    fds[1].fd     = server_args->sockfd[1];
    fds[1].events = POLLIN;

    for(int i = 2; i < MAX_FDS; i++)
    {
        fds[i].fd     = -1;
        fds[i].events = 0;
        // fds[i].revents = 0;
    }

    while(running)
    {
        ssize_t retval;

        // PRINT_VERBOSE("%s\n", "polling...");

        retval = poll(fds, MAX_FDS, -1);
        PRINT_DEBUG("retval: %zd\n", retval);
        if(retval == -1)
        {
            if(errno == EINTR)
            {
                break;
            }
            perror("poll error");
            break;
        }
        if(fds[0].revents & POLLIN)
        {
            client_fd = accept(*server_args->fd, NULL, 0);
            PRINT_DEBUG("client_fd: %d\n", client_fd);
            if(client_fd < 0)
            {
                if(errno == EINTR)
                {
                    break;
                }
                perror("Accept failed");
                continue;
            }

            // Add new client to poll list
            added = 0;
            for(int i = 2; i < MAX_FDS; i++)
            {
                if(fds[i].fd == -1)
                {
                    fds[i].fd      = client_fd;
                    fds[i].events  = POLLIN;
                    fds[i].revents = 0;
                    added          = 1;
                    break;
                }
            }
            if(!added)
            {
                char too_many[] = "Too many clients, rejecting connection\n";

                printf("%s", too_many);
                write_fully(client_fd, &too_many, (ssize_t)strlen(too_many), &server_args->err);

                close(client_fd);
                continue;
            }
        }
        if(fds[1].revents & POLLIN)
        {
            int fd_num;

            if(recv_number(fds[1].fd, &fd_num) < 0)
            {
                perror("recv_num error");
            }

            PRINT_VERBOSE("%s fd: %d \n", "receiving fd from worker...", fd_num);

            for(int i = 2; i < MAX_CLIENTS; i++)
            {
                if(fds[i].fd == fd_num)
                {
                    PRINT_VERBOSE("%s fd: %d \n", "closing fd server side...", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd     = -1;
                    fds[i].events = 0;
                    break;
                }
            }
        }
        // Check existing clients for data
        for(int i = 2; i < MAX_FDS; i++)
        {
            if(fds[i].fd != -1)
            {
                printf("%d %d %d\n", i, fds[i].fd, fds[i].revents);
                if(fds[i].revents & POLLIN)
                {
                    PRINT_VERBOSE("%s fd: %d num: %d\n", "Dispatching to workers...", fds[i].fd, fds[i].fd);

                    fds[i].events = 0;
                    send_fd(server_args->sockfd[0], fds[i].fd, fds[i].fd);
                    fds[i].revents = 0;
                    continue;
                }

                if(fds[i].revents & (POLLHUP | POLLERR))
                {
                    // Client disconnected or error, close and clean up
                    printf("oops...\n");
                    close(fds[i].fd);
                    fds[i].fd     = -1;
                    fds[i].events = 0;
                    continue;
                }
            }
        }
    }
    return END;
}

int main(int argc, char *argv[], char *envp[])
{
    int    retval;
    args_t args;
    int    server_fd;
    pid_t  pid;

    while(*envp)
    {
        PRINT_DEBUG("%s\n", *envp);
        envp++;
    }

    setup_signal();

    printf("Server launching... (press Ctrl+C to interrupt)\n");

    retval = EXIT_SUCCESS;

    memset(&args, 0, sizeof(args_t));

    get_arguments(&args, argc, argv);

    printf("verbose: %d\n", verbose);
    printf("running: %d\n", running);
    PRINT_VERBOSE("%s\n", "verbose on");
    PRINT_DEBUG("%s\n", "debug on");

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, args.sockfd) == -1)
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

    // monitor
    if(pid == 0)
    {
        pid_t pids[NUM_WORKERS];

        PRINT_VERBOSE("%s\n", "monitor");

        PRINT_VERBOSE("%s\n", "creating workers...");
        // workers
        for(int i = 0; i < NUM_WORKERS; i++)
        {
            pids[i] = fork();
            if(pids[i] < 0)
            {
                perror("fork failed");
                exit(EXIT_FAILURE);
            }
            else if(pids[i] == 0)
            {
                request_t request;

                PRINT_VERBOSE("%s\n", "workers spawned");

                memset(&request, 0, sizeof(request_t));

                request.raw = (char *)malloc(RAW_SIZE);
                if(!request.raw)
                {
                    perror("failed to malloc");
                    exit(EXIT_FAILURE);
                }
                memset(request.raw, 0, RAW_SIZE);
                request.sockfd = args.sockfd[0];
                while(running)
                {
                    request.client_fd = recv_fd(args.sockfd[1], &request.fd_num);
                    if(request.client_fd == -1)
                    {
                        continue;
                    }

                    PRINT_VERBOSE("%s fd: %d num: %d\n", "receiving fd from monitor...", request.client_fd, request.fd_num);

                    request.worker_id = i;

                    worker_process(&request);
                }
                memset(request.raw, 0, RAW_SIZE);
                free(request.raw);
            }
        }

        while(running)
        {
            int status;

            pid_t exited_pid = waitpid(-1, &status, 0);

            if(exited_pid > 0)
            {
                PRINT_VERBOSE("Worker (PID: %d) exited. Restarting...\n", exited_pid);

                PRINT_VERBOSE("%s\n", "creating workers...");
                for(int i = 0; i < NUM_WORKERS; i++)
                {
                    if(pids[i] == exited_pid)
                    {
                        pids[i] = fork();
                        if(pids[i] < 0)
                        {
                            perror("fork failed");
                            exit(EXIT_FAILURE);
                        }
                        else if(pids[i] == 0)
                        {
                            request_t request;

                            PRINT_VERBOSE("%s\n", "workers spawned");

                            memset(&request, 0, sizeof(request_t));

                            request.raw = (char *)malloc(RAW_SIZE);
                            if(!request.raw)
                            {
                                perror("failed to malloc");
                                exit(EXIT_FAILURE);
                            }
                            memset(request.raw, 0, RAW_SIZE);
                            request.sockfd = args.sockfd[0];

                            while(running)
                            {
                                request.client_fd = recv_fd(args.sockfd[1], &request.fd_num);
                                if(request.client_fd == -1)
                                {
                                    continue;
                                }

                                PRINT_VERBOSE("%s fd: %d num: %d\n", "receiving fd from monitor...", request.client_fd, request.fd_num);

                                request.worker_id = i;

                                worker_process(&request);
                            }
                            memset(request.raw, 0, RAW_SIZE);
                            free(request.raw);
                        }
                        break;
                    }
                }
            }
        }
        exit(EXIT_SUCCESS);
    }
    else
    {
        server_fd = tcp_server(args.addr, args.port, BACKLOG, &args.err);
        if(server_fd < 0)
        {
            fprintf(stderr, "main::tcp_server: Failed to create TCP server. %d\n", args.err);
            return EXIT_FAILURE;
        }

        printf("Listening on %s:%d\n", args.addr, args.port);

        args.fd = &server_fd;

        event_loop(&args);

        memset(args.buf, 0, BUF_SIZE);
        close(server_fd);
    }

    return retval;
}
