#include <sys/resource.h>
#include "pddl/pddl.h"
#include "opts.h"
#include "print_to_file.h"

static struct {
    int help;
    int version;
    int max_mem;
    char *log_out;
    char *prop_out;

    char *train;
    char *eval;
} opt;

static pddl_err_t err = PDDL_ERR_INIT;
static FILE *log_out = NULL;
static FILE *prop_out = NULL;
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
    optsAddStr("prop-out", 0x0, &opt.prop_out, 0x0,
               "Set output file for properties log.");
    optsAddStr("train", 't', &opt.train, NULL,
               "Train ASNets and save the model to the specified file.");
    optsAddStr("eval", 'e', &opt.eval, NULL,
               "Evaluate model stored in the specified file.");

    if (opts(&argc, argv) != 0)
        return -1;

    if (argc != 2){
        for (int i = 1; i < argc; ++i){
            fprintf(stderr, "Error: Unrecognized argument: %s\n", argv[i]);
        }
        help(argv[0], stderr);
        return -1;
    }

    if ((opt.train == NULL && opt.eval == NULL)
            || (opt.train != NULL && opt.eval != NULL)){
        fprintf(stderr, "Error: Either --train or --eval option must be used.\n");
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
        pddlErrWarnEnable(&err, log_out);
        pddlErrInfoEnable(&err, log_out);
    }

    if (opt.prop_out != NULL){
        prop_out = openFile(opt.prop_out);
        pddlErrPropEnable(&err, prop_out);
    }

    if (opt.max_mem > 0){
        struct rlimit mem_limit;
        mem_limit.rlim_cur
            = mem_limit.rlim_max = opt.max_mem * 1024UL * 1024UL;
        setrlimit(RLIMIT_AS, &mem_limit);
    }

    config_file = argv[1];

    PDDL_LOG(&err, "Version: %{version}s", pddl_version);
    return 0;
}

int main(int argc, char *argv[])
{
    pddlErrStartCtxTimer(&err);
    pddl_timer_t timer;
    pddlTimerStart(&timer);

    if (parseOpts(argc, argv) != 0){
        if (pddlErrIsSet(&err)){
            fprintf(stderr, "Error: ");
            pddlErrPrint(&err, 1, stderr);
        }
        return -1;
    }

    pddl_asnets_config_t cfg;
    if (pddlASNetsConfigInitFromFile(&cfg, config_file, &err) != 0){
        if (pddlErrIsSet(&err)){
            fprintf(stderr, "Error: ");
            pddlErrPrint(&err, 1, stderr);
        }
        return -1;
    }

    pddl_asnets_t *asnets = pddlASNetsNew(&cfg, &err);
    if (asnets == NULL){
        if (pddlErrIsSet(&err)){
            fprintf(stderr, "Error: ");
            pddlErrPrint(&err, 1, stderr);
        }
        return -1;
    }

    int ret = 0;
    if (opt.train != NULL){
        ret = pddlASNetsTrain(asnets, &err);
        if (ret == 0){
            ret = pddlASNetsSave(asnets, opt.train, &err);
        }else{
            if (pddlErrIsSet(&err)){
                fprintf(stderr, "Error: ");
                pddlErrPrint(&err, 1, stderr);
            }
        }

    }else if (opt.eval != NULL){
        ret = pddlASNetsLoad(asnets, opt.eval, &err);
        if (ret < 0){
            fprintf(stderr, "Error: ");
            pddlErrPrint(&err, 1, stderr);
            return -1;
        }

        // TODO: Run evaluation of input problems
    }

    pddlTimerStop(&timer);
    PDDL_LOG(&err, "Overall Elapsed Time: %{overall_elapsed_time}.4fs",
             pddlTimerElapsedInSF(&timer));

    pddlASNetsConfigFree(&cfg);
    pddlASNetsDel(asnets);
    if (log_out != NULL)
        closeFile(log_out);
    if (prop_out != NULL)
        closeFile(prop_out);
    return ret;
}
