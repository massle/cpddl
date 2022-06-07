/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>
 *
 *  This file is part of cpddl.
 *
 *  Distributed under the OSI-approved BSD License (the "License");
 *  see accompanying file BDS-LICENSE for details or see
 *  <http://www.opensource.org/licenses/bsd-license.php>.
 *
 *  This software is distributed WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the License for more information.
 */

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include "pddl/subprocess.h"
#include "internal.h"

#define CMD_BUFSIZ 256
#define READ_INIT_BUFSIZ 256
static void logCommand(char *const argv[], pddl_err_t *err)
{
    char cmd[CMD_BUFSIZ];
    int written = 0;
    for (int i = 0; argv[i] != NULL; ++i)
        written += snprintf(cmd + written, CMD_BUFSIZ - written, " %s", argv[i]);
    LOG(err, "Command:%s", cmd);
}

int pddlExecvp(char *const argv[],
               pddl_exec_status_t *status,
               const char *write_stdin,
               int write_stdin_size,
               char **read_stdout,
               int *read_stdout_size,
               char **read_stderr,
               int *read_stderr_size,
               pddl_err_t *err)
{
    CTX(err, "execvp", "exec");
    logCommand(argv, err);
    fflush(stdout);
    fflush(stderr);
    pddlErrFlush(err);

    if (status != NULL)
        bzero(status, sizeof(*status));
    if (read_stdout != NULL){
        *read_stdout = NULL;
        *read_stdout_size = 0;
    }
    if (read_stderr != NULL){
        *read_stderr = NULL;
        *read_stderr_size = 0;
    }

    int fd_stdin[2] = { -1, -1 };
    int fd_stdout[2] = { -1, -1 };
    int fd_stderr[2] = { -1, -1 };

    int read_stdout_alloc = 0;
    int read_stderr_alloc = 0;
    int written = 0;
    if (write_stdin != NULL){
        if (pipe(fd_stdin) != 0){
            perror("pipe() failed");
            CTXEND(err);
            return -1.;
        }
    }
    if (read_stdout != NULL){
        if (pipe(fd_stdout) != 0){
            if (fd_stdin[0] >= 0)
                close(fd_stdin[0]);
            if (fd_stdin[1] >= 0)
                close(fd_stdin[1]);
            perror("pipe() failed");
            CTXEND(err);
            return -1.;
        }
    }

    if (read_stderr != NULL){
        if (pipe(fd_stderr) != 0){
            if (fd_stdin[0] >= 0)
                close(fd_stdin[0]);
            if (fd_stdin[1] >= 0)
                close(fd_stdin[1]);
            if (fd_stdout[0] >= 0)
                close(fd_stdout[0]);
            if (fd_stdout[1] >= 0)
                close(fd_stdout[1]);
            perror("pipe() failed");
            CTXEND(err);
            return -1.;
        }
    }

    int pid = fork();
    if (pid == 0){
        if (fd_stdin[1] >= 0)
            close(fd_stdin[1]);
        if (fd_stdin[0] >= 0){
            ASSERT_RUNTIME(dup2(fd_stdin[0], STDIN_FILENO) == STDIN_FILENO);
            close(fd_stdin[0]);
        }

        if (fd_stdout[0] >= 0)
            close(fd_stdout[0]);
        if (fd_stdout[1] >= 0){
            ASSERT_RUNTIME(dup2(fd_stdout[1], STDOUT_FILENO) == STDOUT_FILENO);
            close(fd_stdout[1]);
        }else{
            int fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (fd >= 0){
                ASSERT_RUNTIME(dup2(fd, STDOUT_FILENO) == STDOUT_FILENO);
                close(fd);
            }
        }

        if (fd_stderr[0] >= 0)
            close(fd_stderr[0]);
        if (fd_stderr[1] >= 0){
            ASSERT_RUNTIME(dup2(fd_stderr[1], STDERR_FILENO) == STDERR_FILENO);
            close(fd_stderr[1]);
        }else{
            int fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (fd >= 0){
                ASSERT_RUNTIME(dup2(fd, STDERR_FILENO) == STDERR_FILENO);
                close(fd);
            }
        }

        execvp(argv[0], argv);
        FATAL2("exec failed!");

    }else if (pid > 0){
        struct pollfd pfd[3];
        int pfdsize = 0;

        if (fd_stdin[0] >= 0)
            close(fd_stdin[0]);
        if (fd_stdin[1] >= 0){
            pfd[pfdsize].fd = fd_stdin[1];
            pfd[pfdsize].events = POLLOUT | POLLWRBAND | POLLHUP;
            ++pfdsize;
        }

        if (fd_stdout[1] >= 0)
            close(fd_stdout[1]);
        if (fd_stdout[0] >= 0){
            pfd[pfdsize].fd = fd_stdout[0];
            pfd[pfdsize].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLHUP;
            ++pfdsize;
        }

        if (fd_stderr[1] >= 0)
            close(fd_stderr[1]);
        if (fd_stderr[0] >= 0){
            pfd[pfdsize].fd = fd_stderr[0];
            pfd[pfdsize].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLHUP;
            ++pfdsize;
        }

        int rpoll = 0;
        while (pfdsize > 0 && (rpoll = poll(pfd, pfdsize, -1)) > 0){
            pfdsize = 0;
            int fdi = 0;
            if (fd_stdin[1] >= 0){
                if ((pfd[fdi].revents & POLLOUT)
                        || (pfd[fdi].revents & POLLWRBAND)){
                    int remaining = write_stdin_size - written;
                    ssize_t w = write(fd_stdin[1], write_stdin, remaining);
                    if (w > 0)
                        written += w;
                    if (written == write_stdin_size){
                        close(fd_stdin[1]);
                        fd_stdin[1] = -1;
                    }

                }else if (pfd[fdi].revents & POLLHUP){
                    close(fd_stdin[1]);
                    fd_stdin[1] = -1;
                }

                if (fd_stdin[1] >= 0){
                    pfd[pfdsize].fd = fd_stdin[1];
                    pfd[pfdsize].events = POLLOUT | POLLWRBAND | POLLHUP;
                    ++pfdsize;
                }
                ++fdi;
            }

            if (fd_stdout[0] >= 0){
                if ((pfd[fdi].revents & POLLIN)
                        || (pfd[fdi].revents & POLLRDNORM)
                        || (pfd[fdi].revents & POLLRDBAND)
                        || (pfd[fdi].revents & POLLPRI)){
                    if (read_stdout_alloc == *read_stdout_size){
                        if (read_stdout_alloc == 0)
                            read_stdout_alloc = READ_INIT_BUFSIZ;
                        read_stdout_alloc *= 2;
                        *read_stdout = REALLOC_ARR(*read_stdout, char,
                                                   read_stdout_alloc);
                    }
                    int remain = read_stdout_alloc - *read_stdout_size;
                    ssize_t r = read(fd_stdout[0], *read_stdout, remain);
                    if (r > 0)
                        *read_stdout_size += r;

                }else if (pfd[fdi].revents & POLLHUP){
                    close(fd_stdout[0]);
                    fd_stdout[0] = -1;
                }

                if (fd_stdout[0] >= 0){
                    pfd[pfdsize].fd = fd_stdout[0];
                    pfd[pfdsize].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLHUP;
                    ++pfdsize;
                }
                ++fdi;
            }

            if (fd_stderr[0] >= 0){
                if ((pfd[fdi].revents & POLLIN)
                        || (pfd[fdi].revents & POLLRDNORM)
                        || (pfd[fdi].revents & POLLRDBAND)
                        || (pfd[fdi].revents & POLLPRI)){
                    if (read_stderr_alloc == *read_stderr_size){
                        if (read_stderr_alloc == 0)
                            read_stderr_alloc = READ_INIT_BUFSIZ;
                        read_stderr_alloc *= 2;
                        *read_stderr = REALLOC_ARR(*read_stderr, char,
                                                   read_stderr_alloc);
                    }
                    int remain = read_stderr_alloc - *read_stderr_size;
                    ssize_t r = read(fd_stderr[0], *read_stderr, remain);
                    if (r > 0)
                        *read_stderr_size += r;

                }else if (pfd[fdi].revents & POLLHUP){
                    close(fd_stderr[0]);
                    fd_stderr[0] = -1;
                }

                if (fd_stderr[0] >= 0){
                    pfd[pfdsize].fd = fd_stderr[0];
                    pfd[pfdsize].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLHUP;
                    ++pfdsize;
                }
            }
        }

        int wstatus;
        wait(&wstatus);
        if (WIFEXITED(wstatus)){
            if (status != NULL){
                status->exited = 1;
                status->exit_status = WEXITSTATUS(wstatus);
            }

        }else if (WIFSIGNALED(wstatus)){
            if (status != NULL){
                status->signaled = 1;
                status->signum = WTERMSIG(wstatus);
            }
        }

    }else{
        perror("fork failed: ");
        CTXEND(err);
        return -1;
    }

    if (fd_stdin[1] >= 0)
        close(fd_stdin[1]);
    if (fd_stdout[0] >= 0)
        close(fd_stdout[0]);
    if (fd_stderr[0] >= 0)
        close(fd_stderr[0]);

    if (write_stdin != NULL)
        LOG(err, "Written %d / %d", written, write_stdin_size);

    if (read_stdout != NULL){
        LOG(err, "Read %d bytes from stdout, allocated %d bytes",
            *read_stdout_size, read_stdout_alloc);
    }

    if (read_stderr != NULL){
        LOG(err, "Read %d bytes from stderr, allocated %d bytes",
            *read_stderr_size, read_stderr_alloc);
    }

    if (status != NULL){
        LOG(err, "status: exited: %d, exit_status: %d,"
            " signaled: %d, signum: %d (%s)",
            status->exited, status->exit_status,
            status->signaled, status->signum,
            (status->signaled ? strsignal(status->signum) : "" ));
    }

    CTXEND(err);
    return 0;
}
