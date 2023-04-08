# distutils: include_dirs = ../

cdef extern from "pddl/pddl.h":
    ctypedef struct FILE:
        pass
    FILE *stdout
    FILE *stderr

    ctypedef struct pddl_err_t:
        pass
    void pddlErrInit(pddl_err_t *)
    void pddlErrPrint(const pddl_err_t *err, int with_traceback, FILE *fout)
    void pddlErrWarnEnable(pddl_err_t *err, FILE *fout)
    void pddlErrInfoEnable(pddl_err_t *err, FILE *fout)
    void pddlErrInfoDisablePrintResources(pddl_err_t *err, int disable)
    void pddlErrFlush(pddl_err_t *err)

    ctypedef struct pddl_files_t:
        const char *domain_pddl
        const char *problem_pddl
    int pddlFiles(pddl_files_t *files, const char *s1, const char *s2,
                  pddl_err_t *err);
    

    ctypedef struct pddl_config_t:
        int force_adl
        int normalize;
        int remove_empty_types;
        int compile_away_cond_eff;
        int enforce_unit_cost;
        int keep_all_actions;

    ctypedef struct pddl_t:
        pass

    int pddlInit(pddl_t *,const char *domain, const char *problem,
                 const pddl_config_t *cfg, pddl_err_t *err)
    void pddlInitCopy(pddl_t *dst, const pddl_t *src)
    void pddlFree(pddl_t *pddl)
    void pddlNormalize(pddl_t *pddl)
    void pddlCompileAwayCondEff(pddl_t *pddl)
    void pddlPrintPDDLDomain(const pddl_t *pddl, FILE *fout)
    void pddlPrintPDDLProblem(const pddl_t *pddl, FILE *fout)
