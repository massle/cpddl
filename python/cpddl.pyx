cimport cpddl

from cpython.version cimport PY_MAJOR_VERSION

cdef bytes _s(s):
    if type(s) is unicode:
        # Fast path for most common case(s).
        return s.encode('utf-8')

    elif type(s) is str:
        return s.encode('utf-8')

    else:
        raise TypeError("Could not convert {0} to string.".format(s))


cdef class Pddl:
    cdef cpddl.pddl_err_t err
    cdef cpddl.pddl_t pddl

    def __cinit__(self, domain_pddl = None,
                        problem_pddl = None,
                        int force_adl = True,
                        int normalize = False,
                        int remove_empty_types = False,
                        int compile_away_cond_eff = False,
                        int enforce_unit_cost = False,
                        int keep_all_actions = False,
                        log_to = 'stderr'):
        cpddl.pddlErrInit(&self.err)
        if log_to == 'stderr':
            cpddl.pddlErrInfoEnable(&self.err, cpddl.stderr)
            cpddl.pddlErrWarnEnable(&self.err, cpddl.stderr)
        elif log_to == 'stdout':
            cpddl.pddlErrInfoEnable(&self.err, cpddl.stdout)
            cpddl.pddlErrWarnEnable(&self.err, cpddl.stdout)
        else:
            # TODO: raise Exception
            raise

        cdef cpddl.pddl_files_t files
        ret = -1
        if domain_pddl is not None and problem_pddl is not None:
            ret = cpddl.pddlFiles(&files, _s(domain_pddl),
                                  _s(problem_pddl), &self.err)
        elif domain_pddl is None and problem_pddl is not None:
            ret = cpddl.pddlFiles(&files, NULL, _s(problem_pddl), &self.err)
        else:
            # TODO
            raise
        if ret != 0:
            pddlErrPrint(&self.err, 1, cpddl.stderr)
            # TODO
            raise

        cdef cpddl.pddl_config_t pddl_config
        # TODO: Initialize pddl_config to default values
        pddl_config.force_adl = force_adl
        pddl_config.normalize = normalize
        pddl_config.remove_empty_types = remove_empty_types
        pddl_config.compile_away_cond_eff = compile_away_cond_eff
        pddl_config.enforce_unit_cost = enforce_unit_cost
        pddl_config.keep_all_actions = keep_all_actions
        ret = cpddl.pddlInit(&self.pddl, files.domain_pddl, files.problem_pddl,
                             &pddl_config, &self.err)
        if ret != 0:
            print(ret)
            pddlErrPrint(&self.err, 1, cpddl.stderr)
            # TODO
            raise

    def __dealloc__(self):
        cpddl.pddlFree(&self.pddl)

    def normalize(self):
        cpddl.pddlNormalize(&self.pddl)
    def compileAwayCondEff(self):
        cpddl.pddlCompileAwayCondEff(&self.pddl)
