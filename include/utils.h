// cppcheck-suppress-file unusedStructMember

#ifndef SIG_UTILS_H
#define SIG_UTILS_H

#include <signal.h>

// clang-format off
#define PRINT_VERBOSE(fmt, ...) do { if (verbose >= 1) printf(fmt, __VA_ARGS__); } while (0)
#define PRINT_DEBUG(fmt, ...) do { if (verbose >= 2) printf(fmt, __VA_ARGS__); } while (0)
// clang-format on

// 1 = Verbose, 2 = Debug
extern int                   verbose;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,-warnings-as-errors)
extern volatile sig_atomic_t running;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,-warnings-as-errors)

typedef struct
{
    int sockfd;
    int worker_id;
    int fd_num;
    int client_fd;
} worker_t;

void setup_signal(void);

#endif
