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

#ifndef __PDDL_SUBPROCESS_H__
#define __PDDL_SUBPROCESS_H__

#include <pddl/err.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pddl_exec_status {
    /** True if the sub-processes terminated by exit() */
    int exited;
    /** If .exited is true, .exit_status is set to the exit status of the
     *  sub-process */
    int exit_status;
    /** True if the sub-process was terminated by a signal */
    int signaled;
    /** If .signaled is true, .signum is set to the signal number that
     *  terminated the sub-process */
    int signum;
    /** True if the process was killed due to time out */
    pddl_bool_t timed_out;
};
typedef struct pddl_exec_status pddl_exec_status_t;


/**
 * Runs the given command as a sub-process.
 * The status struct is filled with the exit status of the sub-process.
 * The buffer write_stdin is written to the standard input of the
 * sub-process, and buffers read_stdout and read_stderr are allocated and
 * filled with stdout and stderr outputs of the subprocess.
 */
int pddlExecvp(char *const argv[],
               pddl_exec_status_t *status,
               const char *write_stdin,
               int write_stdin_size,
               char **read_stdout,
               int *read_stdout_size,
               char **read_stderr,
               int *read_stderr_size,
               pddl_err_t *err);

/**
 * Same as pddlExecvp() but allows to set time and memory limit -- negative
 * numbers mean "unlimited".
 */
int pddlExecvpLimits(char *const argv[],
                     pddl_exec_status_t *status,
                     const char *write_stdin,
                     int write_stdin_size,
                     char **read_stdout,
                     int *read_stdout_size,
                     char **read_stderr,
                     int *read_stderr_size,
                     float time_limit_in_s,
                     int mem_limit_in_mb,
                     pddl_err_t *err);

/**
 * Run the function fn() in a subprocess and communicate input and output
 * via a shared memory.
 * Before fork(), data_size bytes of shared memory is allocated and
 * in_out_data is copied there. Then fn() is called with the pointer to
 * that memory, and after return the shared memory is copied back to
 * in_out_data.
 * userdata is passed to fn() as its second argument as it is.
 * status is filled with the exit status of the sub-process.
 */
int pddlForkSharedMem(int (*fn)(void *sharedmem, void *userdata),
                      void *in_out_data,
                      size_t data_size,
                      void *userdata,
                      pddl_exec_status_t *status,
                      pddl_err_t *err);

/**
 * Run fn() in a subprocess and collect output written by the function to
 * the file descriptor fdout, i.e., fn() can write to fdout and the parent
 * process collects the output into out buffer.
 * userdata is passed to fn() as its second argument as it is.
 * status is filled with the exit status of the sub-process.
 */
int pddlForkPipe(int (*fn)(int fdout, void *userdata),
                 void *userdata,
                 void **out,
                 int *out_size,
                 pddl_exec_status_t *status,
                 pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_SUBPROCESS_H__ */
