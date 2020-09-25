-include Makefile.local
-include Makefile.include

CFLAGS += -I.
CFLAGS += -Wno-sizeof-pointer-div
CFLAGS += $(BORUVKA_CFLAGS)
CFLAGS += $(BLISS_CFLAGS)
CFLAGS += $(CLIQUER_CFLAGS)
CFLAGS += $(CUDD_CFLAGS)

CPPFLAGS += -Wno-ignored-attributes
CPPFLAGS += -I.
CPPFLAGS += $(BORUVKA_CFLAGS)
CPPFLAGS += $(CPOPTIMIZER_CPPFLAGS)

CPPCHECK_FLAGS += --platform=unix64 --enable=all -I. -Ithird-party/boruvka

TARGETS  = libpddl.a

OBJS  = lisp
OBJS += require
OBJS += type
OBJS += param
OBJS += obj
OBJS += pred
OBJS += fact
OBJS += action
OBJS += prep_action
OBJS += pddl
OBJS += cond
OBJS += cond_arr
OBJS += strips
OBJS += strips_op
OBJS += strips_fact_cross_ref
OBJS += strips_ground_tree
OBJS += strips_ground
OBJS += action_args
OBJS += ground_atom
OBJS += profile
OBJS += helper
OBJS += lifted_mgroup
OBJS += lifted_mgroup_infer
OBJS += lifted_mgroup_htable
OBJS += mgroup
OBJS += mutex_pair
OBJS += pddl_file
OBJS += plan_file
OBJS += irrelevance
OBJS += h1
OBJS += h2
OBJS += h3
OBJS += hm
OBJS += disambiguation
OBJS += bitset
OBJS += set
OBJS += fdr_var
OBJS += fdr_part_state
OBJS += fdr_op
OBJS += fdr
OBJS += fdr_state_packer
OBJS += fdr_state_pool
OBJS += fdr_state_space
OBJS += sym
OBJS += famgroup
OBJS += pot
OBJS += lm_cut
OBJS += hpot
OBJS += hflow
OBJS += hmax
OBJS += hadd
OBJS += pq
OBJS += mg_strips
OBJS += preprocess
OBJS += cg
OBJS += graph
OBJS += clique
OBJS += biclique
OBJS += fdr_app_op
OBJS += random_walk
OBJS += open_list
OBJS += open_list_splaytree1
OBJS += open_list_splaytree2
OBJS += search_astar
OBJS += plan
OBJS += heur
OBJS += heur_blind
OBJS += heur_lm_cut
OBJS += heur_hmax
OBJS += heur_hadd
OBJS += heur_pot_state
OBJS += heur_flow
OBJS += dtg
OBJS += scc
OBJS += ts
OBJS += op_mutex_pair
OBJS += op_mutex_infer
OBJS += op_mutex_infer_ts
OBJS += op_mutex_sym_redundant
OBJS += reversibility
OBJS += cascading_table
OBJS += transition
OBJS += label
OBJS += labeled_transition
OBJS += trans_system
OBJS += trans_system_abstr_map
OBJS += trans_system_graph
OBJS += bdd
OBJS += bdds
OBJS += symbolic_task
OBJS += cost

OBJS_CPP = endomorphism

OBJS := $(foreach obj,$(OBJS),.objs/$(obj).o) $(foreach obj,$(OBJS_CPP),.objs/$(obj).cpp.o)

all: $(TARGETS)

libpddl.a: $(OBJS)
	ar cr $@ $(OBJS)
	ranlib $@

pddl/config.h:
	echo "#ifndef __PDDL_CONFIG_H__" >$@
	echo "#define __PDDL_CONFIG_H__" >>$@
	echo "" >>$@
	if [ "$(DEBUG)" = "yes" ]; then echo "#define PDDL_DEBUG" >>$@; fi
	if [ "$(USE_CLIQUER)" = "yes" ]; then echo "#define PDDL_CLIQUER" >>$@; fi
	if [ "$(USE_CUDD)" = "yes" ]; then echo "#define PDDL_CUDD" >>$@; fi
	echo '#include <boruvka/lp.h>' >__lp.c
	echo 'int main(int argc, char *arvg[]) { return borLPSolverAvailable(BOR_LP_DEFAULT); }' >>__lp.c
	$(CC) $(CFLAGS) -o __lp __lp.c $(BORUVKA_LDFLAGS) $(LP_LDFLAGS) -pthread -lrt -lm
	if ! ./__lp; then echo "#define PDDL_LP" >>$@; fi
	rm -f __lp.c __lp
	echo '#define IL_STD' >__cpopt.c
	echo '#include <ilcp/cp.h>' >>__cpopt.c
	echo '#include <ilcplex/cpxconst.h>' >>__cpopt.c
	echo 'int main(int argc, char *arvg[]) { IloCP cp(); return 0; }' >>__cpopt.c
	if $(CXX) $(CPPFLAGS) -o __cpopt __cpopt.c $(CPOPTIMIZER_LDFLAGS) -pthread -lrt -lm 2>/dev/null; then if ./__cpopt; then echo "#define PDDL_CPOPTIMIZER" >>$@; fi; fi
	rm -f __cpopt.c __cpopt
	echo "" >>$@
	echo "#endif /* __PDDL_CONFIG_H__ */" >>$@

.objs/%.o: src/%.c pddl/%.h pddl/config.h
	$(CC) $(CFLAGS) -c -o $@ $<
.objs/%.o: src/%.c pddl/config.h
	$(CC) $(CFLAGS) -c -o $@ $<
.objs/%.cpp.o: src/%.cpp pddl/%.h pddl/config.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<
.objs/%.cpp.o: src/%.cpp pddl/config.h
	$(CXX) $(CPPFLAGS) -c -o $@ $<

%.h: pddl/config.h
%.c: pddl/config.h


clean:
	rm -f $(OBJS)
	rm -f .objs/*.o
	rm -f $(TARGETS)
	rm -f pddl/config.h
	rm -f src/*.pb.{cc,h}
	if [ -d bin ]; then $(MAKE) -C bin clean; fi;
	if [ -d test ]; then $(MAKE) -C test clean; fi;
	if [ -d doc ]; then $(MAKE) -C doc clean; fi;

mrproper: clean boruvka-clean opts-clean bliss-clean lpsolve-clean cudd-clean

check:
	$(MAKE) -C test check
check-noreg:
	$(MAKE) -C test check-noreg
check-ci:
	$(MAKE) -C test check-ci
check-valgrind:
	$(MAKE) -C test check-valgrind
check-segfault:
	$(MAKE) -C test check-segfault
static-check:
	$(CPPCHECK) $(CPPCHECK_FLAGS) pddl/ src/

doc:
	$(MAKE) -C doc

analyze: clean
	$(SCAN_BUILD) $(MAKE)

list-global-symbols: libpddl.a
	readelf -s libpddl.a \
        | grep GLOBAL \
        | awk '{print $$8}' \
        | sort \
        | uniq \
        | grep -v '^pddl' \
        | grep -v '^bor' \
        | grep -v '^_bor' \
        | grep -v '^__bor' \
        | grep -v '^_Z.*Ilo' \
        | less

third-party: boruvka opts bliss cudd
third-party-clean: boruvka-clean opts-clean bliss-clean cudd-clean

boruvka: third-party/boruvka/Makefile
	$(MAKE) $(_BOR_MAKE_DEF) -C third-party/boruvka all
boruvka-clean:
	$(MAKE) -C third-party/boruvka clean
third-party/boruvka/Makefile:
	git submodule init -- third-party/boruvka
	git submodule update -- third-party/boruvka

opts: third-party/opts/Makefile
	$(MAKE) -C third-party/opts all
opts-clean:
	$(MAKE) -C third-party/opts clean
third-party/opts/Makefile:
	git submodule init -- third-party/opts
	git submodule update -- third-party/opts

bliss: third-party/bliss/libbliss.a
bliss-clean:
	rm -rf third-party/bliss
third-party/bliss/libbliss.a:
	rm -rf third-party/bliss
	cd third-party && unzip bliss-$(BLISS_VERSION).zip
	mv third-party/bliss-$(BLISS_VERSION) third-party/bliss
	cd third-party/bliss && patch -p1 <../bliss-0.73-memleak.patch
	$(MAKE) CC=$(CXX) -C third-party/bliss

lpsolve: third-party/lpsolve/liblpsolve.a
lpsolve-clean:
	$(MAKE) -C third-party/lpsolve clean
third-party/lpsolve/liblpsolve.a:
	$(MAKE) -C third-party/lpsolve

cudd: third-party/cudd/libcudd.a
cudd-clean:
	git clean -fdx third-party/cudd
	rm -f third-party/cudd/lib*.a
	rm -f third-party/cudd/cudd.h
third-party/cudd/libcudd.a:
	cd third-party/cudd && aclocal
	cd third-party/cudd && autoconf
	cd third-party/cudd && automake
	cd third-party/cudd && ./configure --disable-shared
	$(MAKE) -C third-party/cudd
	cp third-party/cudd/cudd/.libs/libcudd.a $@
	cp third-party/cudd/cudd/cudd.h third-party/cudd/cudd.h

.PHONY: all clean check check-ci check-valgrind help doc install analyze \
  examples mrproper \
  third-party third-party-clean \
  boruvka boruvka-clean \
  opts opts-clean \
  bliss bliss-clean \
  lpsolve lpsolve-clean
