#include "pddl/pddl.h"
#include "pddl/asnets_convert_from_sql.h"
#include "opts.h"
#include "print_to_file.h"

static struct {
    pddl_bool_t help;
    pddl_bool_t version;
    int max_mem;
    char *log_out;

    char *eval_out;
} opt;

static enum {
    CMD_TRAIN,
    CMD_EVAL,
    CMD_GEN_FD_ENC,
    CMD_GEN_FD_OSP_ENC,
    CMD_CONVERT_OLD_MODEL
} cmd;


static pddl_err_t err = PDDL_ERR_INIT;
static FILE *log_out = NULL;

static void help(const char *argv0, FILE *fout)
{
    fprintf(fout, "version: %s\n", pddl_version);
    fprintf(fout, "Usage: %s COMMAND [OPTIONS] ...\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "COMMAND train:\n");
    fprintf(fout, "  %s train [OPTIONS] config.toml model-file-prefix\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "  Train an ASNets model according to the configuration.\n");
    fprintf(fout, "  A model after each epoch is written into a file prefixed with the\n");
    fprintf(fout, "  {model-file-prefix} argument.\n");
    fprintf(fout, "\n");
    fprintf(fout, "\n");
    fprintf(fout, "COMMAND eval:\n");
    fprintf(fout, "  %s eval [OPTIONS] input-model-file domain.pddl problem.pddl ...\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "  Evaluate the ASNets model stored in {input-model-file} and all specified\n");
    fprintf(fout, "  pddl problems.\n");
    fprintf(fout, "\n");
    fprintf(fout, "\n");
    fprintf(fout, "COMMAND gen-fd-encoding:\n");
    fprintf(fout, "  %s gen-fd-encoding domain.pddl problem.pddl output.fd\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "  Generate FD encoding of the specified task as it is used by ASNets.\n");
    fprintf(fout, "\n");
    fprintf(fout, "\n");
    fprintf(fout, "COMMAND gen-fd-osp-encoding:\n");
    fprintf(fout, "  %s gen-fd-osp-encoding domain.pddl problem.pddl output.fd\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "  Same as gen-fd-encoding, but it uses a variant of FD format\n");
    fprintf(fout, "  for OSP tasks and all goals are specified as soft-goals.\n");\
    fprintf(fout, "\n");
    fprintf(fout, "\n");
    fprintf(fout, "COMMAND convert-old-model:\n");
    fprintf(fout, "  %s convert-old-model input-file output-file\n", argv0);
    fprintf(fout, "\n");
    fprintf(fout, "  Convert old sqlite-based model files to the current format:\n");
    fprintf(fout, "\n");
    fprintf(fout, "\n");
    fprintf(fout, "OPTIONS:\n");
    optsPrint(fout);
}

static void shiftCmdArgs(int *argc, char *argv[])
{
    for (int i = 2; i < *argc; ++i)
        argv[i - 1] = argv[i];
    *argc -= 1;
}

static int parseOpts(int *argc, char *argv[])
{
    optsAddFlag("help", 'h', &opt.help, 0, "Print this help.");
    optsAddFlag("version", 0x0, &opt.version, 0, "Print version and exit.");
    optsAddInt("max-mem", 0x0, &opt.max_mem, 0,
               "Maximum memory in MB if >0.");
    optsAddStr("log-out", 0x0, &opt.log_out, "stdout",
               "Set output file for logs.");

    optsAddStr("eval-out", 0x0, &opt.eval_out, NULL,
               "This takes effect only for the 'eval' command."
               " If set, it specifies a prefix for files where found plans"
               " are written, namely they are written to {prefix}-{task_index}.plan.");

    if (*argc <= 1){
        help(argv[0], stderr);
        return -1;
    }

    if (strcmp(argv[1], "train") == 0){
        shiftCmdArgs(argc, argv);
        cmd = CMD_TRAIN;

    }else if (strcmp(argv[1], "eval") == 0){
        shiftCmdArgs(argc, argv);
        cmd = CMD_EVAL;

    }else if (strcmp(argv[1], "gen-fd-encoding") == 0){
        shiftCmdArgs(argc, argv);
        cmd = CMD_GEN_FD_ENC;

    }else if (strcmp(argv[1], "gen-fd-osp-encoding") == 0){
        shiftCmdArgs(argc, argv);
        cmd = CMD_GEN_FD_OSP_ENC;

    }else if (strcmp(argv[1], "convert-old-model") == 0){
        shiftCmdArgs(argc, argv);
        cmd = CMD_CONVERT_OLD_MODEL;

    }else{
        fprintf(stderr, "Error: Unknown command %s\n", argv[1]);
        help(argv[0], stderr);
        return -1;
    }

    if (opts(argc, argv) != 0)
        return -1;

    if (opt.help){
        help(argv[0], stderr);
        return -1;
    }

    if (cmd == CMD_TRAIN){
        if (*argc != 3){
            fprintf(stderr, "Error: Command train expects two arguments.");
            help(argv[0], stderr);
            return -1;
        }

    }else if (cmd == CMD_EVAL){
        if (*argc < 4){
            fprintf(stderr, "Error: Missing arguments to the command eval.\n");
            help(argv[0], stderr);
            return -1;
        }

    }else if (cmd == CMD_GEN_FD_ENC){
        if (*argc != 4){
            fprintf(stderr, "Error: Command gen-fd-encoding requires 3 arguments.\n");
            help(argv[0], stderr);
            return -1;
        }

    }else if (cmd == CMD_GEN_FD_OSP_ENC){
        if (*argc != 4){
            fprintf(stderr, "Error: Command gen-fd-osp-encoding requires 3 arguments.\n");
            help(argv[0], stderr);
            return -1;
        }

    }else if (cmd == CMD_CONVERT_OLD_MODEL){
        if (*argc != 3){
            fprintf(stderr, "Error: Command convert-old-model takes exactly"
                    " two additional arguments, but %d were given.\n",
                    *argc - 1);
            help(argv[0], stderr);
            return -1;
        }
    }

    return 0;
}

static int train(int argc, char *argv[])
{
    pddl_asnets_config_t cfg;
    if (pddlASNetsConfigInitFromFile(&cfg, argv[1], &err) != 0)
        PDDL_TRACE_RET(&err, -1);

    cfg.save_model_prefix = argv[2];

    pddl_asnets_t *asnets = pddlASNetsNew(&cfg, &err);
    if (asnets == NULL)
        PDDL_TRACE_RET(&err, -1);

    return pddlASNetsTrain(asnets, &err);
}

struct eval_stats {
    pddl_bool_t solved;
    int plan_length;
    int osp_goal_size;
    int osp_msgs_size;
};

static int evaluate(int argc, char *argv[])
{
    pddl_asnets_t *asnets = pddlASNetsNewLoad(argv[1], argv[2], &err);
    if (asnets == NULL)
        PDDL_TRACE_RET(&err, -1);

    const pddl_asnets_config_t *cfg = pddlASNetsGetConfig(asnets);
    const pddl_asnets_lifted_task_t *lt = pddlASNetsGetLiftedTask(asnets);

    int policy_rollout_limit = cfg->policy_rollout_limit;

    int num_probs = argc - 3;
    struct eval_stats stats[num_probs];

    for (int pi = 0; pi < num_probs; ++pi){
        PDDL_CTX(&err, "Task %d", pi);
        pddl_asnets_ground_task_t gt;
        int st = pddlASNetsGroundTaskInit(&gt, lt, argv[2], argv[3 + pi],
                                          cfg, &err);
        if (st != 0){
            pddlASNetsDel(asnets);
            PDDL_CTXEND(&err);
            PDDL_TRACE_RET(&err, -1);
        }

        PDDL_LOG(&err, "Rollout of policy ...");
        pddl_asnets_policy_rollout_t rollout;
        pddlASNetsPolicyRolloutInit(&rollout, &gt);
        pddlASNetsPolicyRollout(asnets, &rollout, &gt, policy_rollout_limit, &err);
        PDDL_LOG(&err, "Found plan: %s", (rollout.found_plan ? "true" : "false"));
        PDDL_LOG(&err, "Num states: %d", rollout.states.num_states);
        PDDL_LOG(&err, "Num ops: %d", pddlIArrSize(&rollout.ops));
        PDDL_LOG(&err, "Plan size: %d", pddlIArrSize(&rollout.plan));
        PDDL_LOG(&err, "OSP reached goal size: %d",
                 rollout.osp_reached_goal_size);

        stats[pi].solved = rollout.found_plan;
        stats[pi].plan_length = -1;
        if (rollout.found_plan)
            stats[pi].plan_length = pddlIArrSize(&rollout.plan);
        stats[pi].osp_goal_size = rollout.osp_reached_goal_size;
        stats[pi].osp_msgs_size = gt.osp_msgs_size_for_init;

        if (rollout.found_plan && opt.eval_out != NULL){
            char fn[512];
            snprintf(fn, 511, "%s-%06d.plan", opt.eval_out, pi);
            FILE *fout = fopen(fn, "w");
            if (fout != NULL){
                int op_id;
                PDDL_IARR_FOR_EACH(&rollout.plan, op_id){
                    fprintf(fout, "(%s)\n", gt.fdr.op.op[op_id]->name);
                }
                fclose(fout);

            }else{
                pddlASNetsPolicyRolloutFree(&rollout);
                pddlASNetsGroundTaskFree(&gt);
                pddlASNetsDel(asnets);
                PDDL_CTXEND(&err);
                PDDL_ERR_RET(&err, -1, "Could not open file %s", fn);
            }
        }

        pddlASNetsPolicyRolloutFree(&rollout);
        pddlASNetsGroundTaskFree(&gt);
        PDDL_CTXEND(&err);
    }

    for (int pi = 0; pi < num_probs; ++pi){
        if (cfg->osp_all_soft_goals){
            PDDL_LOG(&err, "Task %d solved: %s, length: %d, goal size: %d/%d :: %s",
                     pi, (stats[pi].solved ? "true" : "false" ),
                     stats[pi].plan_length, stats[pi].osp_goal_size,
                     stats[pi].osp_msgs_size, argv[pi + 3]);
        }else{
            PDDL_LOG(&err, "Task %d solved: %s, length: %d :: %s",
                     pi, (stats[pi].solved ? "true" : "false"),
                     stats[pi].plan_length, argv[pi + 3]);
        }
    }

    pddlASNetsDel(asnets);
    return 0;
}

static int genFDEnc(int argc, char *argv[], int osp)
{
    pddl_asnets_lifted_task_t lt;
    if (pddlASNetsLiftedTaskInit(&lt, argv[1], &err) != 0){
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }

    pddl_asnets_config_t cfg;
    pddlASNetsConfigInit(&cfg);

    pddl_asnets_ground_task_t gt;
    if (pddlASNetsGroundTaskInit(&gt, &lt, argv[1], argv[2], &cfg, &err) != 0){
        pddlASNetsLiftedTaskFree(&lt);
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }

    pddl_fdr_write_config_t wcfg = PDDL_FDR_WRITE_CONFIG_INIT;
    wcfg.filename = argv[3];
    if (osp)
        wcfg.osp_all_soft_goals = pddl_true;
    wcfg.fd = pddl_true;
    pddlFDRWrite(&gt.fdr, &wcfg);

    pddlASNetsGroundTaskFree(&gt);
    pddlASNetsLiftedTaskFree(&lt);
    return 0;
}

static int convertOldModel(int argc, char *argv[])
{
    return pddlASNetsConvertFromSql(argv[1], argv[2], &err);
}

int main(int argc, char *argv[])
{
    pddl_timer_t timer;
    pddlTimerStart(&timer);

    if (parseOpts(&argc, argv) != 0){
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }

    if (opt.log_out != NULL){
        log_out = openFile(opt.log_out);
        pddlErrLogEnable(&err, log_out);
    }

    if (opt.max_mem > 0){
        struct rlimit mem_limit;
        mem_limit.rlim_cur = mem_limit.rlim_max = opt.max_mem * 1024UL * 1024UL;
        setrlimit(RLIMIT_AS, &mem_limit);
    }

    PDDL_LOG(&err, "Version: %s", pddl_version);

    int ret = 0;
    switch (cmd){
        case CMD_TRAIN:
            ret = train(argc, argv);
            break;

        case CMD_EVAL:
            ret = evaluate(argc, argv);
            break;

        case CMD_GEN_FD_ENC:
            ret = genFDEnc(argc, argv, 0);
            break;

        case CMD_GEN_FD_OSP_ENC:
            ret = genFDEnc(argc, argv, 1);
            break;

        case CMD_CONVERT_OLD_MODEL:
            ret = convertOldModel(argc, argv);
            break;
    }

    if (ret != 0)
        pddlErrPrint(&err, 1, stderr);

    if (log_out != NULL)
        closeFile(log_out);
    return ret;
}
