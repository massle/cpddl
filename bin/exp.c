#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "pddl/pddl.h"
#include "opts.h"

#define PATHSIZE 512

#define COMMAND_GEN 1
#define COMMAND_RUN 2
#define TARGET_RCI_CPU 1

struct {
    int help;
    int command;
    char *progpath;
    char *run_script;
    char *topdir;
    int task_id;
    int max_time;
    int max_mem;
    char *bench_path;
    int target;
} cfg;

static pddl_err_t err = PDDL_ERR_INIT;

static void cleanTaskDir(const char *dir)
{
    char fn[PATHSIZE];
    snprintf(fn, PATHSIZE - 1, "%s/task.out", dir);
    unlink(fn);
    snprintf(fn, PATHSIZE - 1, "%s/task.err", dir);
    unlink(fn);
    snprintf(fn, PATHSIZE - 1, "%s/task.finished", dir);
    unlink(fn);
    snprintf(fn, PATHSIZE - 1, "%s/task.memout", dir);
    unlink(fn);
    snprintf(fn, PATHSIZE - 1, "%s/task.timeout", dir);
    unlink(fn);
    snprintf(fn, PATHSIZE - 1, "%s/task.time", dir);
    unlink(fn);
}

static int writeFileInDir(const char *dir,
                          const char *fname,
                          const char *cont)
{
    char fn[PATHSIZE];
    snprintf(fn, PATHSIZE - 1, "%s/%s", dir, fname);
    FILE *fout = fopen(fn, "w");
    if (fout == NULL){
        fprintf(stderr, "Error: Failed to create file %s", fn);
        return -1;
    }
    fprintf(fout, "%s", cont);
    fclose(fout);

    return 0;
}

static int writeTimeFileInDir(const char *dir,
                              const char *fname,
                              const pddl_timer_t *timer)
{
    char c[512];
    sprintf(c, "%f\n", pddlTimerElapsedInSF(timer));
    return writeFileInDir(dir, fname, c);
}

static int setConfig(int argc, char *argv[])
{
    optsAddFlag("help", 'h', &cfg.help, 0, "Print help");
    optsAddStr("dir", 'D', &cfg.topdir, NULL,
               "Diretory where the task will run.");
    optsAddInt("max-time", 'T', &cfg.max_time, 1800,
               "Maximum time in seconds.");
    optsAddInt("max-mem", 'M', &cfg.max_mem, 8192,
               "Maximum memory in MB.");
    optsAddStr("bench", 'B', &cfg.bench_path, NULL,
               "Path to the directory with benchmark tasks.");
    optsAddIntSwitch("target", 't', &cfg.target,
                     "Target queue/host/... One of:"
                     " rci-cpu",
                     1,
                     "rci-cpu", TARGET_RCI_CPU);

    int ret = opts(&argc, argv);
    if (ret != 0)
        cfg.help = 1;

    if (argc > 1){
        if (strcmp(argv[1], "gen") == 0){
            cfg.command = COMMAND_GEN;
            if (argc == 4){
                cfg.topdir = argv[2];
                cfg.run_script = argv[3];
            }else{
                fprintf(stderr, "Error: Invalid command.\n");
                cfg.help = 1;
            }

        }else if (strcmp(argv[1], "run") == 0){
            cfg.command = COMMAND_RUN;
            if (argc == 3){
                cfg.task_id = atoi(argv[2]);
            }else{
                fprintf(stderr, "Error: Invalid command.\n");
                cfg.help = 1;
            }
        }
    }

    if (cfg.command == 0){
        fprintf(stderr, "Error: no command!\n");
        cfg.help = 1;
    }

    if (cfg.command == COMMAND_GEN){
        if (cfg.bench_path == NULL || !pddlIsDir(cfg.bench_path)){
            fprintf(stderr, "Error: Bench must be specified.\n");
            cfg.help = 1;
        }
        if (cfg.topdir == NULL || pddlIsDir(cfg.topdir)){
            fprintf(stderr, "Error: dst-dir must not exist.\n");
            cfg.help = 1;
        }
        if (cfg.run_script == NULL || !pddlIsFile(cfg.run_script)){
            fprintf(stderr, "Error: run-script %s does not exist.\n",
                    cfg.run_script);
            cfg.help = 1;
        }

    }else if (cfg.command == COMMAND_RUN){
        if (cfg.topdir == NULL || !pddlIsDir(cfg.topdir)){
            fprintf(stderr, "Error: directory %s does not exist.\n",
                    cfg.topdir);
            cfg.help = 1;
        }
    }


    if (cfg.help){
        fprintf(stderr, "Usage: %s [OPTIONS] gen dst-dir run-script\n", argv[0]);
        optsPrint(stderr);
        return -1;
    }

    if (cfg.run_script != NULL)
        cfg.run_script = realpath(cfg.run_script, NULL);

    cfg.progpath = realpath(argv[0], NULL);

    PDDL_INFO(&err, "cfg.progpath = '%s'", cfg.progpath);
    PDDL_INFO(&err, "cfg.run_script = '%s'", cfg.run_script);
    PDDL_INFO(&err, "cfg.topdir = '%s'", cfg.topdir);
    PDDL_INFO(&err, "cfg.task_id = %d", cfg.task_id);
    PDDL_INFO(&err, "cfg.max_time = %ds", cfg.max_time);
    PDDL_INFO(&err, "cfg.max_mem = %dMB", cfg.max_mem);
    PDDL_INFO(&err, "cfg.bench = '%s'", cfg.bench_path);
    if (cfg.target == TARGET_RCI_CPU){
        PDDL_INFO2(&err, "cfg.target = rci-cpu");
    }else{
        PDDL_INFO2(&err, "cfg.target = none");
    }
    return 0;
}

static int genRunFile(char *fn, int offset)
{
    if (offset == 0){
        snprintf(fn, PATHSIZE - 1, "%s/run.sh", cfg.topdir);
    }else{
        snprintf(fn, PATHSIZE - 1, "%s/run-%d.sh", cfg.topdir, offset);
    }
    FILE *fout = fopen(fn, "w");
    if (fout == NULL){
        fprintf(stderr, "Error: Failed to create file %s", fn);
        return -1;
    }

    fprintf(fout, "#!/bin/bash\n");

    if (cfg.target == TARGET_RCI_CPU){
        int mem = cfg.max_mem + 50;
        int max_time = cfg.max_time + 30;
        int days = max_time / (24 * 3600);
        int hours = (max_time % (24 * 3600)) / 3600;
        int minutes = ((max_time % (24 * 3600)) % 3600) / 60;
        int seconds = ((max_time % (24 * 3600)) % 3600) % 60;
        if (max_time < 4 * 3600){
            fprintf(fout, "#SBATCH -p cpufast # partition (queue)\n");
        }else{
            fprintf(fout, "#SBATCH -p cpu # partition (queue)\n");
        }
        fprintf(fout, "#SBATCH -N 1 # number of nodes\n");
        fprintf(fout, "#SBATCH -n 1 # number of cores\n");
        fprintf(fout, "#SBATCH --cpus-per-task=2\n");
        fprintf(fout, "##SBATCH -w X # specific nodes\n");
        fprintf(fout, "#SBATCH -x n[21-33] # exclude nodes\n");
        fprintf(fout, "#SBATCH --mem %dM # memory limit\n", mem);
        fprintf(fout, "#SBATCH -t %d-%d:%d:%d # time (D-HH:MM:SS)\n",
                days, hours, minutes, seconds);
        fprintf(fout, "#SBATCH -o %s/%%6a/run.out # STDOUT\n", cfg.topdir);
        fprintf(fout, "#SBATCH -e %s/%%6a/run.err # STDOUT\n", cfg.topdir);
        fprintf(fout, "##SBATCH --hint=nomultithread\n");

        fprintf(fout, "\n");
        if (offset == 0){
            fprintf(fout, "ID=${SLURM_ARRAY_TASK_ID}\n");
        }else{
            fprintf(fout, "ID=$((${SLURM_ARRAY_TASK_ID} + %d))\n", offset);
        }
    }

    fprintf(fout, "\n");
    fprintf(fout, "%s", cfg.progpath);
    fprintf(fout, " --dir %s", cfg.topdir);
    fprintf(fout, " --max-time %d", cfg.max_time);
    fprintf(fout, " --max-mem %d", cfg.max_mem);
    fprintf(fout, " run ${ID}");
    fprintf(fout, "\n");
    fclose(fout);

    return 0;
}

static int genRunMakefile(const pddl_bench_t *bench)
{
    char fn[PATHSIZE];
    snprintf(fn, PATHSIZE - 1, "%s/Makefile", cfg.topdir);
    FILE *fout = fopen(fn, "w");
    if (fout == NULL){
        fprintf(stderr, "Error: Failed to create file %s", fn);
        return -1;
    }

    for (int ti = 0; ti < bench->task_size; ++ti){
        fprintf(fout, "TASK += %s/%06d/task.finished\n", cfg.topdir, ti);
    }
    fprintf(fout, "\n");
    fprintf(fout, "all: $(TASK)\n");
    fprintf(fout, "\n");
    for (int ti = 0; ti < bench->task_size; ++ti){
        fprintf(fout, "%s/%06d/task.finished:\n", cfg.topdir, ti);
        fprintf(fout, "\t");
        fprintf(fout, "%s", cfg.progpath);
        fprintf(fout, " --dir %s", cfg.topdir);
        fprintf(fout, " --max-time %d", cfg.max_time);
        fprintf(fout, " --max-mem %d", cfg.max_mem);
        fprintf(fout, " run %d", ti);
        fprintf(fout, "\n");
    }
    fclose(fout);

    return 0;
}

static void taskDir(const char *base, int id, char *dir)
{
    snprintf(dir, PATHSIZE - 1, "%s/%06d", base, id);
}

static int cmdGen(void)
{
    pddl_bench_t bench;
    pddlBenchInit(&bench);
    pddlBenchLoadDir(&bench, cfg.bench_path);
    if (bench.task_size == 0){
        fprintf(stderr, "Empty benchmark directory %s\n", cfg.bench_path);
        return -1;
    }

    pddl_rand_t rnd;
    pddlRandInit(&rnd);
    for (int ti = bench.task_size - 1; ti > 0; --ti){
        int idx = pddlRand(&rnd, 0, ti);
        if (idx != ti){
            pddl_bench_task_t tmp = bench.task[ti];
            bench.task[ti] = bench.task[idx];
            bench.task[idx] = tmp;
        }
    }
    PDDL_INFO(&err, "Found %d tasks", bench.task_size);

    if (mkdir(cfg.topdir, 0755) != 0){
        fprintf(stderr, "Error: Failed to create directory %s\n", cfg.topdir);
        return -1;
    }
    cfg.topdir = realpath(cfg.topdir, NULL);

    char dir[PATHSIZE];
    for (int ti = 0; ti < bench.task_size; ++ti){
        const pddl_bench_task_t *task = bench.task + ti;
        taskDir(cfg.topdir, ti, dir);
        if (mkdir(dir, 0755) != 0){
            fprintf(stderr, "Error: Failed to create directory %s\n", dir);
            return -1;
        }

        char fn[PATHSIZE];
        snprintf(fn, PATHSIZE - 1, "%s/domain.pddl", dir);
        if (symlink(task->pddl_files.domain_pddl, fn) != 0){
            fprintf(stderr, "Error: Failed to create symlink %s -> %s\n",
                    fn, task->pddl_files.domain_pddl);
            return -1;
        }

        snprintf(fn, PATHSIZE - 1, "%s/problem.pddl", dir);
        if (symlink(task->pddl_files.problem_pddl, fn) != 0){
            fprintf(stderr, "Error: Failed to create symlink %s -> %s\n",
                    fn, task->pddl_files.domain_pddl);
            return -1;
        }

        snprintf(fn, PATHSIZE - 1, "%s/run.sh", dir);
        if (symlink(cfg.run_script, fn) != 0){
            fprintf(stderr, "Error: Failed to create symlink %s -> %s\n",
                    fn, task->pddl_files.domain_pddl);
            return -1;
        }

        snprintf(fn, PATHSIZE - 1, "%s/task.prop", dir);
        FILE *fout = fopen(fn, "w");
        if (fout == NULL){
            fprintf(stderr, "Error: Failed to create file %s", fn);
            return -1;
        }
        fprintf(fout, "domain_pddl = \"%s\"\n", task->pddl_files.domain_pddl);
        fprintf(fout, "problem_pddl = \"%s\"\n", task->pddl_files.problem_pddl);
        fprintf(fout, "bench_name = \"%s\"\n", task->bench_name);
        fprintf(fout, "domain_name = \"%s\"\n", task->domain_name);
        fprintf(fout, "problem_name = \"%s\"\n", task->problem_name);
        fprintf(fout, "optimal_cost = %d\n", task->optimal_cost);
        fclose(fout);
    }
    pddlBenchFree(&bench);

    char fn[PATHSIZE];
    snprintf(fn, PATHSIZE - 1, "%s/submit.sh", cfg.topdir);
    FILE *fout = fopen(fn, "w");
    if (fout == NULL){
        fprintf(stderr, "Error: Failed to create file %s", fn);
        return -1;
    }
    fprintf(fout, "#!/bin/bash\n");
    fprintf(fout, "set -e\n");

    fprintf(fout, "if [ -f %s/submitted ]; then\n", cfg.topdir);
    fprintf(fout, "    echo \"%s already submitted\"\n", cfg.topdir);
    fprintf(fout, "    exit 0\n");
    fprintf(fout, "fi\n");
    fprintf(fout, "\n");

    if (cfg.target == TARGET_RCI_CPU){
        char fnrun[PATHSIZE];
        for (int i = 0; i < bench.task_size; i += 1000){
            if (genRunFile(fnrun, i) != 0)
                return -1;
            int maxid = PDDL_MIN(999, bench.task_size - i - 1);
            fprintf(fout, "sbatch --array=0-%d %s\n", maxid, fnrun);
        }
    }
    fprintf(fout, "\n");

    fprintf(fout, "touch %s/submitted\n", cfg.topdir);
    fclose(fout);
    genRunMakefile(&bench);

    PDDL_INFO2(&err, "Done.");
    return 0;
}

static void terminateTask(int pid)
{
    struct timespec timeout;
    timeout.tv_sec = 3;
    timeout.tv_nsec = 0;

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    PDDL_INFO2(&err, "Sending SIGTERM and wait at most 3 seconds.");
    kill(pid, SIGTERM);

    siginfo_t info;
    int retsig = sigtimedwait(&sigset, &info, &timeout);
    if (retsig < 0){
        if (errno == EAGAIN){
            PDDL_INFO2(&err, "Task not terminated within 3 seconds.");
            PDDL_INFO2(&err, "Sending SIGKILL and wait");
            kill(pid, SIGKILL);

            int wstatus;
            wait(&wstatus);
            PDDL_INFO2(&err, "Task exited.");
            PDDL_INFO(&err, "Exit status: %d", WEXITSTATUS(wstatus));

        }else if (errno == EINTR){
            PDDL_INFO2(&err, "sigtimedwait failed with err EINTR");
        }else if (errno == EINVAL){
            PDDL_INFO2(&err, "sigtimedwait failed with err EINVAL");
        }else{
            PDDL_INFO(&err, "sigtimedwait failed with errno %d", errno);
        }

    }else{
        switch (retsig){
            case SIGCHLD:
                PDDL_INFO(&err, "Child terminated with status %d.",
                          info.si_status);
                PDDL_INFO(&err, "Exit status: %d", info.si_status);
                break;
            default:
                PDDL_INFO(&err, "Caught signal %d (%s)",
                          retsig, strsignal(retsig));
        }
    }
}

static int cmdRun(void)
{
    char topdir[PATHSIZE];
    taskDir(cfg.topdir, cfg.task_id, topdir);
    PDDL_INFO(&err, "Task directory: %s", topdir);

    cleanTaskDir(topdir);

    pddl_timer_t timer;
    pddlTimerStart(&timer);
    PDDL_INFO2(&err, "Forking...");
    int pid = fork();
    if (pid == 0){
        char memlimit[32];
        sprintf(memlimit, "MemoryMax=%dM", cfg.max_mem);

        chdir(topdir);
        int fdout = open("task.out", O_WRONLY|O_CREAT, 0644);
        int fderr = open("task.err", O_WRONLY|O_CREAT, 0644);
        if (fdout < 0 || fderr < 0){
            fprintf(stderr, "Error: Could not create output files!\n");
            exit(-1);
        }

        close(1);
        dup(fdout);
        close(2);
        dup(fderr);
        execl("/usr/bin/systemd-run",
              "/usr/bin/systemd-run",
              "--user",
              "--scope",
              "-p", memlimit,
              "/bin/bash", "./run.sh",
              NULL);

    }else if (pid > 0){
        struct timespec timeout;
        timeout.tv_sec = cfg.max_time;
        timeout.tv_nsec = 0;

        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGCHLD);
        sigprocmask(SIG_BLOCK, &sigset, NULL);

        siginfo_t info;
        int retsig = sigtimedwait(&sigset, &info, &timeout);
        if (retsig < 0){
            if (errno == EAGAIN){
                PDDL_INFO2(&err, "Task timed out");
                PDDL_INFO2(&err, "Terminating the task");
                terminateTask(pid);
                writeFileInDir(topdir, "task.timeout", "");

            }else if (errno == EINTR){
                PDDL_INFO2(&err, "sigtimedwait failed with err EINTR");
            }else if (errno == EINVAL){
                PDDL_INFO2(&err, "sigtimedwait failed with err EINVAL");
            }else{
                PDDL_INFO(&err, "sigtimedwait failed with errno %d", errno);
            }

        }else{
            switch (retsig){
                case SIGCHLD:
                    PDDL_INFO(&err, "Child terminated with status %d.",
                              info.si_status);
                    PDDL_INFO(&err, "Exit status: %d", info.si_status);
                    if (info.si_status != 0){
                        if (WIFSIGNALED(info.si_status)){
                            PDDL_INFO(&err, "Terminated with signal %d (%s)",
                                      WTERMSIG(info.si_status),
                                      strsignal(WTERMSIG(info.si_status)));
                            if (WTERMSIG(info.si_status) == SIGKILL){
                                PDDL_INFO2(&err, "Probably ran out of memory");
                                writeFileInDir(topdir, "task.memout", "");
                            }
                        }
                    }
                    break;
                default:
                    PDDL_INFO(&err, "Caught signal %d (%s)",
                              retsig, strsignal(retsig));
            }
        }

    }else{
        perror("fork failed: ");
        return -1;
    }

    pddlTimerStop(&timer);
    writeTimeFileInDir(topdir, "task.time", &timer);
    writeFileInDir(topdir, "task.finished", "");

    return 0;
}

int main(int argc, char *argv[])
{
    pddlErrInfoEnable(&err, stderr);

    if (setConfig(argc, argv) != 0)
        return -1;

    switch(cfg.command){
        case COMMAND_GEN:
            return cmdGen();
        case COMMAND_RUN:
            return cmdRun();
    }
    return 0;
}

