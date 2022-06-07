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
    int exited;
    int exit_status;
    int signaled;
    int signum;
};
typedef struct pddl_exec_status pddl_exec_status_t;


int pddlExecvp(char *const argv[],
               pddl_exec_status_t *status,
               const char *write_stdin,
               int write_stdin_size,
               char **read_stdout,
               int *read_stdout_size,
               char **read_stderr,
               int *read_stderr_size,
               pddl_err_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL_SUBPROCESS_H__ */
