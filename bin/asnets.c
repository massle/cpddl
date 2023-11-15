#include "pddl/pddl.h"
#include "opts.h"
#include "print_to_file.h"

static struct {
    pddl_bool_t help;
    pddl_bool_t version;
    int max_mem;
    char *log_out;

    char *train;
    char *train_save_prefix;
    char *eval;
    char *fd_enc;
    char *fd_enc_osp;
    pddl_bool_t eval_write_plans;
    pddl_bool_t eval_benchmark_trainer;
    char *info;
    char *gen;
} opt;

static pddl_err_t err = PDDL_ERR_INIT;
static FILE *log_out = NULL;
static char *config_file = NULL;

static void help(const char *argv0, FILE *fout)
{
    fprintf(fout, "Usage: %s [OPTIONS] config.toml\n", argv0);
    fprintf(fout, "version: %s\n", pddl_version);
    fprintf(fout, "\n");
    fprintf(fout, "OPTIONS:\n");
    optsPrint(fout);
}

static int parseOpts(int argc, char *argv[])
{
    optsAddFlag("help", 'h', &opt.help, 0, "Print this help.");
    optsAddFlag("version", 0x0, &opt.version, 0, "Print version and exit.");
    optsAddInt("max-mem", 0x0, &opt.max_mem, 0,
               "Maximum memory in MB if >0.");
    optsAddStr("log-out", 0x0, &opt.log_out, "stderr",
               "Set output file for logs.");
    optsAddStr("train", 't', &opt.train, NULL,
               "Train ASNets and save the model to the specified file.");
    optsAddStr("train-save-prefix", 0x0, &opt.train_save_prefix, NULL,
               "If set, the model for every epoch is"
               " saved to the file with this prefix.");
    optsAddStr("eval", 'e', &opt.eval, NULL,
               "Evaluate model stored in the specified file.");
    optsAddStr("fd-enc", 0x0, &opt.fd_enc, NULL,
               "Generate FD encoding of the specified task as it is used"
               " by ASNets into the specified file.");
    optsAddStr("fd-enc-osp", 0x0, &opt.fd_enc_osp, NULL,
               "Same as --fd-enc but encoded OSP version of the task where"
               " all goal facts are soft goals.");
    optsAddFlag("eval-write-plans", 0x0, &opt.eval_write_plans, 0,
                "Write plans to files based on domain and problem names.");
    optsAddFlag("eval-benchmark-trainer", 0x0, &opt.eval_benchmark_trainer, 0,
                "Use solution found by trainer planner as benchamrk.");
    optsAddStr("info", 'i', &opt.info, NULL,
               "Print info about the stored model.");
    optsAddStr("gen", 'g', &opt.gen, NULL,
               "Generate a default configuration file.");

    if (opts(&argc, argv) != 0)
        return -1;

    int need_config = 0;
    if (opt.eval != NULL || opt.train != NULL)
        need_config = 1;

    if (opt.fd_enc != NULL || opt.fd_enc_osp){
        if (argc != 3){
            fprintf(stderr, "Error: Missing domain and/or problem file\n");
            help(argv[0], stderr);
            return -1;
        }

    }else if ((need_config && argc != 2) || (!need_config && argc != 1)){
        if (need_config && argc <= 1){
            fprintf(stderr, "Error: Missing config file\n");
        }else{
            for (int i = 1; i < argc; ++i){
                fprintf(stderr, "Error: Unrecognized argument: %s\n", argv[i]);
            }
        }
        help(argv[0], stderr);
        return -1;
    }

    int req_opts = (int)(opt.train != NULL);
    req_opts += (int)(opt.eval != NULL);
    req_opts += (int)(opt.info != NULL);
    req_opts += (int)(opt.gen != NULL);
    req_opts += (int)(opt.fd_enc != NULL);
    req_opts += (int)(opt.fd_enc_osp != NULL);
    if (req_opts != 1){
        fprintf(stderr, "Error: Either --train, --eval, --info, --gen,"
                " --fd-enc, or --fd-enc-osp option must be used.\n");
        help(argv[0], stderr);
        return -1;
    }

    if (opt.help){
        help(argv[0], stderr);
        return -1;
    }

    if (opt.version){
        fprintf(stdout, "%s\n", pddl_version);
        return 1;
    }

    if (opt.log_out != NULL){
        log_out = openFile(opt.log_out);
        pddlErrLogEnable(&err, log_out);
    }

    if (opt.max_mem > 0){
        struct rlimit mem_limit;
        mem_limit.rlim_cur
            = mem_limit.rlim_max = opt.max_mem * 1024UL * 1024UL;
        setrlimit(RLIMIT_AS, &mem_limit);
    }

    if (opt.fd_enc == NULL && opt.fd_enc_osp == NULL && argc > 1)
        config_file = argv[1];

    PDDL_LOG(&err, "Version: %s", pddl_version);
    return 0;
}

int main(int argc, char *argv[])
{
    pddl_timer_t timer;
    pddlTimerStart(&timer);

    if (parseOpts(argc, argv) != 0){
        pddlErrPrint(&err, 1, stderr);
        return -1;
    }

    if (opt.fd_enc != NULL || opt.fd_enc_osp != NULL){
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
        if (opt.fd_enc != NULL){
            wcfg.filename = opt.fd_enc;
        }else if (opt.fd_enc_osp != NULL){
            wcfg.filename = opt.fd_enc_osp;
            wcfg.osp_all_soft_goals = pddl_true;
        }
        wcfg.fd = pddl_true;
        pddlFDRWrite(&gt.fdr, &wcfg);

        pddlASNetsGroundTaskFree(&gt);
        pddlASNetsLiftedTaskFree(&lt);
        return 0;
    }

    if (opt.train != NULL && pddlIsFile(opt.train)){
        fprintf(stderr, "Error: File %s already exists.\n", opt.train);
        return -1;
    }

    pddl_asnets_config_t cfg;
    if (config_file != NULL){
        if (pddlASNetsConfigInitFromFile(&cfg, config_file, &err) != 0){
            pddlErrPrint(&err, 1, stderr);
            return -1;
        }
    }else{
        pddlASNetsConfigInit(&cfg);
    }

    if (opt.train_save_prefix != NULL)
        cfg.save_model_prefix = opt.train_save_prefix;


    pddl_asnets_t *asnets = NULL;
    if (opt.train != NULL || opt.eval != NULL){
        asnets = pddlASNetsNew(&cfg, &err);
        if (asnets == NULL){
            pddlErrPrint(&err, 1, stderr);
            return -1;
        }
    }

    int ret = 0;
    if (opt.train != NULL){
        ret = pddlASNetsTrain(asnets, &err);
        if (ret == 0){
            ret = pddlASNetsSave(asnets, opt.train, &err);
        }else{
            pddlErrPrint(&err, 1, stderr);
        }

    }else if (opt.eval != NULL){
        ret = pddlASNetsLoad(asnets, opt.eval, &err);
        if (ret < 0){
            pddlErrPrint(&err, 1, stderr);
            return -1;
        }

        pddlASNetsEvaluate(asnets, opt.eval_write_plans, &err);

    }else if (opt.info != NULL){
        ret = pddlASNetsPrintModelInfo(opt.info, &err);
        if (ret < 0){
            pddlErrPrint(&err, 1, stderr);
            return -1;
        }

    }else if (opt.gen != NULL){
        FILE *fout = fopen(opt.gen, "w");
        if (fout != NULL){
            pddlASNetsConfigWrite(&cfg, fout);
            fclose(fout);
        }else{
            fprintf(stderr, "Error: Could not open %s\n", opt.gen);
            ret = -1;
        }
    }

    pddlTimerStop(&timer);
    PDDL_LOG(&err, "Overall Elapsed Time: %.4fs",
             pddlTimerElapsedInSF(&timer));

    pddlASNetsConfigFree(&cfg);
    if (asnets != NULL)
        pddlASNetsDel(asnets);
    if (log_out != NULL)
        closeFile(log_out);
    return ret;
}
