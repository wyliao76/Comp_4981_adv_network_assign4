#include "args.h"
#include "fsm.h"
#include "networking.h"
#include "utils.h"
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#define BACKLOG 5
#define MAX_CLIENTS 64
#define MAX_FDS (MAX_CLIENTS + 2)

static void load_lib(const char *lib_path, void **handle, void (**func)(void *))
{
    PRINT_DEBUG("%s\n", "loading lib...");

    *handle = dlopen(lib_path, RTLD_LAZY);
    if(!*handle)
    {
        printf("dlopen failed: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
    *func = (void (*)(void *))dlsym(*handle, "fsm_run");
#pragma GCC diagnostic pop
    if(!*func)
    {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(*handle);
        exit(EXIT_FAILURE);
    }
}

static ssize_t is_new_lib(const char *lib_path, time_t *last_modified_time)
{
    struct stat file_stat;

    if(stat(lib_path, &file_stat) == -1)
    {
        perror("check dir is dir");
        exit(EXIT_FAILURE);
    }

    if(*last_modified_time < file_stat.st_mtime)
    {
        *last_modified_time = file_stat.st_mtime;
        return 1;
    }
    return 0;
}

static void worker_process(int sockfd, int worker_id)
{
    worker_t   worker_args;
    time_t     last_modified_time;
    const char lib_path[] = "./libmylib.so";
    void      *handle;
    void (*func)(void *);

    memset(&worker_args, 0, sizeof(worker_args));
    worker_args.sockfd    = sockfd;
    worker_args.worker_id = worker_id;
    last_modified_time    = 0;
    handle                = NULL;
    func                  = NULL;

    PRINT_VERBOSE("%s\n", "workers spawned");

    PRINT_DEBUG("Last modified: %s", ctime(&last_modified_time));

    is_new_lib(lib_path, &last_modified_time);
    load_lib(lib_path, &handle, &func);

    PRINT_DEBUG("Last modified: %s", ctime(&last_modified_time));

    while(running)
    {
        worker_args.client_fd = recv_fd(sockfd, &worker_args.fd_num);
        if(worker_args.client_fd <= 0)
        {
            perror("recv_fd error");
        }
        PRINT_VERBOSE("Worker %d (PID: %d) started\n", worker_id, getpid());
        PRINT_VERBOSE("%s fd: %d num: %d\n", "receiving fd from monitor...", worker_args.client_fd, worker_args.fd_num);

        if(is_new_lib(lib_path, &last_modified_time))
        {
            PRINT_DEBUG("%s %s", "new lib found! Unloading lib...", ctime(&last_modified_time));
            dlclose(handle);

            load_lib(lib_path, &handle, &func);
        }

        func(&worker_args);
    }
    PRINT_DEBUG("%s\n", "worker exiting, unloading lib...");
    dlclose(handle);
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
                const char too_many[] = "Too many clients, rejecting connection\n";

                printf("%s", too_many);
                // write_fully(client_fd, &too_many, (ssize_t)strlen(too_many), &server_args->err);

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
                    continue;
                }
            }
        }
        // Check existing clients for data
        for(int i = 2; i < MAX_FDS; i++)
        {
            if(fds[i].fd != -1)
            {
                if(fds[i].revents & POLLIN)
                {
                    PRINT_VERBOSE("%s fd: %d num: %d\n", "Dispatching to workers...", fds[i].fd, fds[i].fd);

                    fds[i].events = 0;
                    send_fd(server_args->sockfd[1], fds[i].fd, fds[i].fd);
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
    pid_t  monitor_pid;

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

    monitor_pid = fork();

    if(monitor_pid == -1)
    {
        fprintf(stderr, "Error creating child process\n");
        exit(EXIT_FAILURE);
    }

    // monitor
    if(monitor_pid == 0)
    {
        pid_t *pids;

        pids = (pid_t *)malloc((size_t)args.workers * sizeof(pid_t));
        if(!pids)
        {
            fprintf(stderr, "failed to malloc\n");
            exit(EXIT_FAILURE);
        }

        PRINT_VERBOSE("%s\n", "monitor");

        PRINT_VERBOSE("creating %d %s\n", args.workers, "workers...");
        // workers
        for(int i = 0; i < args.workers; i++)
        {
            pids[i] = fork();
            if(pids[i] < 0)
            {
                perror("fork failed");
                exit(EXIT_FAILURE);
            }
            else if(pids[i] == 0)
            {
                worker_process(args.sockfd[0], i);
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
                for(int i = 0; i < args.workers; i++)
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
                            worker_process(args.sockfd[0], i);
                        }
                        break;
                    }
                }
            }
        }

        free(pids);
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
