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
#define MAX_CLIENTS 64
#define MAX_FDS (MAX_CLIENTS + 1)
#define NUM_WORKERS 3
#define RAW_SIZE 8192

typedef struct request_t
{
    char      *raw;
    fd_info_t *info;
    int       *sockfd;
    int        worker_id;
    int        err;
} request_t;

static void worker_process(void *args)
{
    const request_t *request = (request_t *)args;
    printf("Worker %d (PID: %d) started\n", request->worker_id, getpid());

    // Worker loop: Replace this with actual worker logic
    while(1)
    {
        printf("Worker %d is handling tasks...\n", request->worker_id);
        sleep(3);    // Simulate work
        send_fd(*request->sockfd, request->info);
        break;
    }
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
        fds[i].fd = -1;
    }

    while(running)
    {
        ssize_t retval;

        // PRINT_VERBOSE("%s\n", "polling...");

        retval = poll(fds, MAX_FDS, -1);
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
            for(int i = 1; i < MAX_FDS; i++)
            {
                if(fds[i].fd == -1)
                {
                    fds[i].fd     = client_fd;
                    fds[i].events = POLLIN;
                    added         = 1;
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
            fd_info_t info;
            if(recv_fd(fds[1].fd, &info) < 0)
            {
                perror("recv_fd error");
            }
            PRINT_VERBOSE("%s fd: %d num: %d\n", "receiving fd from worker...", info.fd, info.fd_num);
            close(fds[info.fd_num].fd);
            fds[info.fd_num].fd     = -1;
            fds[info.fd_num].events = POLLIN;
        }
        // Check existing clients for data
        for(int i = 2; i < MAX_FDS; i++)
        {
            if(fds[i].fd != -1)
            {
                if(fds[i].revents & POLLIN)
                {
                    fd_info_t info;

                    info.fd     = fds[i].fd;
                    info.fd_num = i;
                    PRINT_VERBOSE("%s fd: %d num: %d\n", "Dispatching to workers...", fds[i].fd, fds[i].fd);

                    fds[i].revents = 0;
                    send_fd(server_args->sockfd[0], &info);
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
    // int    sockfd[2];
    int   server_fd;
    pid_t pid;

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
        fd_info_t info;
        request_t request;
        pid_t     pids[NUM_WORKERS];

        PRINT_VERBOSE("%s\n", "monitor");

        request.raw = (char *)malloc(RAW_SIZE);
        if(!request.raw)
        {
            perror("failed to malloc");
            exit(EXIT_FAILURE);
        }
        request.sockfd = &args.sockfd[0];
        request.info   = &info;

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
                // int err;
                PRINT_VERBOSE("%s\n", "workers spawned");
                if(recv_fd(args.sockfd[1], &info) < 0)
                {
                    perror("recv_fd error");
                }
                PRINT_VERBOSE("%s fd: %d num: %d\n", "receiving fd from monitor...", info.fd, info.fd_num);

                request.worker_id = i;

                // if(setSocketNonBlocking(info.fd, &err) < 0)
                // {
                //     perror("set non-blocking failed");
                // }

                worker_process(&request);
                exit(EXIT_SUCCESS);    // Prevent workers from continuing the parent logic
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
                            PRINT_VERBOSE("%s\n", "workers spawned");
                            if(recv_fd(args.sockfd[1], &info) < 0)
                            {
                                perror("recv_fd error");
                            }
                            PRINT_VERBOSE("%s fd: %d num: %d\n", "receiving fd from monitor...", info.fd, info.fd_num);

                            request.worker_id = i;

                            worker_process(&request);
                            exit(EXIT_SUCCESS);
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
