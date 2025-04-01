// cppcheck-suppress-file unusedStructMember

#ifndef ARGS_H
#define ARGS_H

#include <arpa/inet.h>
#include <unistd.h>
#define BUF_SIZE 50
#define ARGC 10

typedef struct args_t
{
    const char *addr;
    in_port_t   port;
    int        *err;
    char        buf[BUF_SIZE];
    char       *argv[2];
    char       *envp[ARGC];
} args_t;

_Noreturn void usage(const char *binary_name, int exit_code, const char *message);

void get_arguments(args_t *args, int argc, char *argv[]);

int convert(const char *str);

#endif    // ARGS_H
