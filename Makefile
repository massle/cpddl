-include Makefile.config
-include Makefile.include

MAKE_FILES := Makefile Makefile.include $(wildcard Makefile.config)

SRC  = alloc
SRC += err
SRC += hfunc
SRC += sha256
SRC += google-city-hash
SRC += toml
SRC += rand
SRC += sort
SRC += qsort
SRC += timsort
SRC += mergesort
SRC += heapsort
SRC += segmarr
SRC += extarr
SRC += pairheap
SRC += hashset
SRC += rbtree
SRC += htable
SRC += fifo
SRC += lp
SRC += lp-cplex
SRC += lp-gurobi
SRC += lp-highs
SRC += cp
SRC += cp-minizinc
SRC += lisp
SRC += require_flags
SRC += type
SRC += param
SRC += obj
SRC += pred
SRC += fact
SRC += action
SRC += prep_action
SRC += pddl
SRC += unify
SRC += compile_in_lifted_mgroup
SRC += fm
SRC += fm_arr
SRC += strips
SRC += strips_op
SRC += strips_fact_cross_ref
SRC += strips_maker
SRC += strips_conj
SRC += sql_grounder
SRC += strips_ground_tree
SRC += strips_ground
SRC += strips_ground_sql
SRC += strips_ground_datalog
SRC += action_args
SRC += ground_atom
SRC += profile
SRC += lifted_mgroup
SRC += lifted_mgroup_infer
SRC += lifted_mgroup_htable
SRC += mgroup
SRC += mgroup_projection
SRC += mutex_pair
SRC += pddl_file
SRC += plan_file
SRC += irrelevance
SRC += h1
SRC += h2
SRC += h3
SRC += hm
SRC += disambiguation
SRC += bitset
SRC += set
SRC += fdr_var
SRC += fdr_part_state
SRC += fdr_op
SRC += fdr
SRC += fdr_state_packer
SRC += fdr_state_pool
SRC += fdr_state_space
SRC += fdr_state_sampler
SRC += strips_state_space
SRC += famgroup
SRC += pot
SRC += lm_cut
SRC += hpot
SRC += hflow
SRC += hmax
SRC += hadd
SRC += hff
SRC += pq
SRC += mg_strips
SRC += preprocess
SRC += cg
SRC += graph
SRC += clique
SRC += biclique
SRC += fdr_app_op
SRC += random_walk
SRC += open_list
SRC += open_list_splaytree1
SRC += open_list_splaytree2
SRC += search
SRC += search_astar
SRC += search_lazy
SRC += lifted_app_action
SRC += lifted_app_action_sql
SRC += lifted_app_action_datalog
SRC += lifted_search
SRC += plan
SRC += relaxed_plan
SRC += heur
SRC += heur_blind
SRC += heur_dead_end
SRC += heur_lm_cut
SRC += heur_hmax
SRC += heur_hadd
SRC += heur_hff
SRC += heur_flow
SRC += heur_op_mutex
SRC += dtg
SRC += scc
SRC += ts
SRC += op_mutex_pair
SRC += op_mutex_infer
SRC += op_mutex_infer_ts
SRC += op_mutex_redundant
SRC += op_mutex_redundant_greedy
SRC += op_mutex_redundant_max
SRC += reversibility
SRC += invertibility
SRC += cascading_table
SRC += transition
SRC += label
SRC += labeled_transition
SRC += trans_system
SRC += trans_system_abstr_map
SRC += trans_system_graph
SRC += bdds
SRC += symbolic_vars
SRC += symbolic_constr
SRC += symbolic_trans
SRC += symbolic_state
SRC += symbolic_task
SRC += symbolic_split_goal
SRC += cost
SRC += black_mgroup
SRC += red_black_fdr
SRC += outbox
SRC += datalog
SRC += datalog_pddl
SRC += endomorphism_fdr
SRC += endomorphism_ts
SRC += endomorphism_lifted
SRC += homomorphism
SRC += homomorphism_heur
SRC += prune_strips
SRC += objset
SRC += iset
SRC += lset
SRC += cset
SRC += iarr
SRC += lifted_heur
SRC += lifted_heur_relaxed
SRC += subprocess
SRC += task
SRC += asnets_task
SRC += asnets_train_data

SRC += __sqlite3

SRC += _version

SRC_CPP =
SRC_CPP += cp-cp-optimizer

SRC_STUB =

ifeq '$(USE_BLISS)' 'yes'
  SRC += sym
else
  SRC_STUB += sym
endif

ifeq '$(USE_CUDD)' 'yes'
  SRC += bdd
else
  SRC_STUB += bdd
endif


ifeq '$(USE_DYNET)' 'yes'
  SRC_CPP += asnets_dynet
else
  SRC_STUB += asnets_dynet
endif

OBJS_PIC := $(foreach obj,$(SRC),.objs/$(obj).pic.o) \
            $(foreach obj,$(SRC_CPP),.objs/$(obj).pic.cpp.o) \
            $(foreach obj,$(SRC_STUB),.objs/$(obj)_stub.pic.o)

OBJS := $(foreach obj,$(SRC),.objs/$(obj).o) \
        $(foreach obj,$(SRC_CPP),.objs/$(obj).cpp.o) \
        $(foreach obj,$(SRC_STUB),.objs/$(obj)_stub.o)

GEN  = pddl/objset.h
GEN += src/objset.c
GEN += pddl/iset.h
GEN += src/iset.c
GEN += pddl/lset.h
GEN += src/lset.c
GEN += pddl/cset.h
GEN += src/cset.c
GEN += pddl/iarr.h
GEN += src/iarr.c
GEN += src/_version.c

all: libpddl.a

bin: libpddl.a
	$(MAKE) -C bin

libpddl.a: $(OBJS) $(MAKE_FILES)
	ar cr $@ $(OBJS)
	ranlib $@

libpddl.pic.a: $(OBJS_PIC) $(MAKE_FILES)
	ar cr $@ $(OBJS_PIC)
	ranlib $@

libpddl.so: $(OBJS_PIC) $(MAKE_FILES)
	$(CC) -shared -o $@ $(OBJS_PIC)

pddl/config.h: $(MAKE_FILES)
	echo "#ifndef __PDDL_CONFIG_H__" >$@
	echo "#define __PDDL_CONFIG_H__" >>$@
	echo "" >>$@
	if [ "$(DEBUG)" = "yes" ]; then echo "#define PDDL_DEBUG" >>$@; fi
	if [ "$(USE_CLIQUER)" = "yes" ]; then echo "#define PDDL_CLIQUER" >>$@; fi
	if [ "$(USE_CUDD)" = "yes" ]; then echo "#define PDDL_CUDD" >>$@; fi
	if [ "$(USE_BLISS)" = "yes" ]; then echo "#define PDDL_BLISS" >>$@; fi
	if [ "$(USE_CPLEX)" = "yes" ]; then echo "#define PDDL_CPLEX" >>$@; fi
	if [ "$(USE_CPOPTIMIZER)" = "yes" ]; then echo "#define PDDL_CPOPTIMIZER" >>$@; fi
	if [ "$(USE_GUROBI)" = "yes" ]; then echo "#define PDDL_GUROBI" >>$@; fi
	if [ "$(USE_HIGHS)" = "yes" ]; then echo "#define PDDL_HIGHS" >>$@; fi
	if [ "$(USE_CPLEX)" = "yes" ] || [ "$(USE_GUROBI)" = "yes" ] || [ "$(USE_HIGHS)" = "yes" ]; then echo "#define PDDL_LP" >>$@; fi
	if [ "$(MINIZINC_BIN)" != "" ]; then echo "#define PDDL_MINIZINC" >>$@; fi
	echo "#define PDDL_MINIZINC_BIN \"$(MINIZINC_BIN)\"" >>$@
	echo "#define PDDL_MINIZINC_VERSION \"$(MINIZINC_VERSION)\"" >>$@
	if [ "$(USE_DYNET)" = "yes" ]; then echo "#define PDDL_DYNET" >>$@; fi
	echo "" >>$@
	echo "#endif /* __PDDL_CONFIG_H__ */" >>$@

pddl/objset.h: src/_set_arr.h scripts/fmt_set.sh
	$(SH) scripts/fmt_set.sh set Set pddl_obj_id_t obj Obj OBJ <$< >$@
src/objset.c: src/_set_arr.c scripts/fmt_set.sh pddl/objset.h
	$(SH) scripts/fmt_set.sh set Set pddl_obj_id_t obj Obj OBJ <$< >$@
pddl/iset.h: src/_set_arr.h scripts/fmt_set.sh
	$(SH) scripts/fmt_set.sh set Set int i I I <$< >$@
src/iset.c: src/_set_arr.c scripts/fmt_set.sh pddl/objset.h
	$(SH) scripts/fmt_set.sh set Set int i I I <$< >$@
pddl/lset.h: src/_set_arr.h scripts/fmt_set.sh
	$(SH) scripts/fmt_set.sh set Set long l L L <$< >$@
src/lset.c: src/_set_arr.c scripts/fmt_set.sh pddl/objset.h
	$(SH) scripts/fmt_set.sh set Set long l L L <$< >$@
pddl/cset.h: src/_set_arr.h scripts/fmt_set.sh
	$(SH) scripts/fmt_set.sh set Set long c C C <$< >$@
src/cset.c: src/_set_arr.c scripts/fmt_set.sh pddl/objset.h
	$(SH) scripts/fmt_set.sh set Set long c C C <$< >$@
pddl/iarr.h: src/_arr.h scripts/fmt_set.sh
	$(SH) scripts/fmt_set.sh arr Arr int i I I <$< >$@
src/iarr.c: src/_arr.c scripts/fmt_set.sh
	$(SH) scripts/fmt_set.sh arr Arr int i I I <$< >$@

src/_version.c: pddl/version.h
	echo "#include \"pddl/version.h\"" >$@
	echo "const char *pddl_build_commit = \"$(shell git rev-parse HEAD)\";" >>$@
	echo "const char *pddl_version = PDDL_VERSION_STR \"-$(shell git rev-parse HEAD)\";" >>$@
.objs/_version.o: src/_version.c pddl/version.h
	$(CC) -I. -c -o $@ $<
.objs/_version.pic.o: src/_version.c pddl/version.h
	$(CC) -I. -fPIC -c -o $@ $<

src/tmp.cudd-version.h: third-party/cudd/libcudd.a
	echo '#include "internal.h"' >src/tmp.cudd-version.c
	echo '#include <cudd/cudd.h>' >>src/tmp.cudd-version.c
	echo '#include <stdio.h>' >>src/tmp.cudd-version.c
	echo "int main(int argc, char *argv[]){ Cudd_PrintVersion(stdout); return 0; }" >>src/tmp.cudd-version.c
	$(CC) $(CFLAGS) $(CUDD_CFLAGS) -o src/tmp.cudd-version src/tmp.cudd-version.c $(CUDD_LDFLAGS) -lm
	echo -n '#define CUDD_VERSION "' >$@
	./src/tmp.cudd-version | tr -d '\n' >>$@
	echo '"' >>$@
	rm -f src/tmp.cudd-version.c
	rm -f src/tmp.cudd-version

.objs/bdd.o: src/bdd.c pddl/bdd.h src/tmp.cudd-version.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) $(CUDD_CFLAGS) -c -o $@ $<
.objs/bdd.pic.o: src/bdd.c pddl/bdd.h src/tmp.cudd-version.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -fPIC $(CUDD_CFLAGS) -c -o $@ $<
.objs/sym.o: src/sym.c pddl/sym.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) $(BLISS_CFLAGS) -c -o $@ $<
.objs/sym.pic.o: src/sym.c pddl/sym.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -fPIC $(BLISS_CFLAGS) -c -o $@ $<
.objs/clique.o: src/clique.c pddl/clique.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) $(CLIQUER_CFLAGS) -c -o $@ $<
.objs/clique.pic.o: src/clique.c pddl/clique.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -fPIC $(CLIQUER_CFLAGS) -c -o $@ $<
.objs/lp-%.o: src/lp-%.c src/_lp.h pddl/lp.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) $(LP_CFLAGS) -c -o $@ $<
.objs/lp-%.pic.o: src/lp-%.c src/_lp.h pddl/lp.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -fPIC $(LP_CFLAGS) -c -o $@ $<
.objs/__sqlite3.o: src/sqlite3.c pddl/config.h
	$(CC) $(SQLITE_CFLAGS) -c -o $@ $<
.objs/__sqlite3.pic.o: src/sqlite3.c pddl/config.h
	$(CC) $(SQLITE_CFLAGS) -fPIC -c -o $@ $<

.objs/cp-cp-optimizer.cpp.o: src/cp-cp-optimizer.cpp src/_cp.h pddl/cp.h pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) $(CPOPTIMIZER_CPPFLAGS) -c -o $@ $<
.objs/cp-cp-optimizer.pic.cpp.o: src/cp-cp-optimizer.cpp src/_cp.h pddl/cp.h pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) $(CPOPTIMIZER_CPPFLAGS) -fPIC -c -o $@ $<
.objs/asnets_dynet.cpp.o: src/asnets_dynet.cpp pddl/asnets.h pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) $(DYNET_CPPFLAGS) -c -o $@ $<
.objs/asnets_dynet.pic.cpp.o: src/asnets_dynet.cpp pddl/asnets.h pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) $(DYNET_CPPFLAGS) -fPIC -c -o $@ $<

src/bdd_stub.c: pddl/bdd.h scripts/gen-stub.sh
	$(SH) scripts/gen-stub.sh $< "Binary decision diagrams require the CUDD library; cpddl must be re-compiled with the CUDD support." pddl_cudd_version >$@
src/sym_stub.c: pddl/sym.h scripts/gen-stub.sh
	$(SH) scripts/gen-stub.sh $< "Symmetries require the Bliss library; cpddl must be re-compiled with the Bliss support." pddl_bliss_version >$@
src/asnets_dynet_stub.c: pddl/asnets.h scripts/gen-stub.sh
	$(SH) scripts/gen-stub.sh $< "ASNets require the DyNet library; cpddl must be re-compiled with the DyNet support." pddl_dynet_version >$@

.objs/%.o: src/%.c pddl/%.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -c -o $@ $<
.objs/%.pic.o: src/%.c pddl/%.h pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<
.objs/%.o: src/%.c pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -c -o $@ $<
.objs/%.pic.o: src/%.c pddl/config.h $(GEN)
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<
.objs/%.cpp.o: src/%.cpp pddl/%.h pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) -c -o $@ $<
.objs/%.pic.cpp.o: src/%.cpp pddl/%.h pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) -fPIC -c -o $@ $<
.objs/%.cpp.o: src/%.cpp pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) -c -o $@ $<
.objs/%.pic.cpp.o: src/%.cpp pddl/config.h $(GEN)
	$(CXX) $(CPPFLAGS) -fPIC -c -o $@ $<

%.h: pddl/config.h
%.c: pddl/config.h


clean: c
	rm -f .objs/*.o

c:
	rm -f .objs/[a-zA-Z0-9]*.o
	rm -f .objs/_[a-zA-Z0-9]*.o
	rm -f *.a
	rm -f *.so
	rm -f pddl/config.h
	rm -f src/*_stub.c
	rm -f src/tmp.*
	rm -f $(GEN)
	if [ -d bin ]; then $(MAKE) -C bin clean; fi;
	if [ -f t/Makefile ]; then $(MAKE) -C t clean; fi;

mrproper: clean third-party-clean

fetch-submodules:
	git submodule update --init --recursive

check check-all check-valgrind check-all-valgrind check-segfault check-all-segfault check-gdb check-all-gdb: libpddl.a
	if [ -f t/Makefile ]; then $(MAKE) -C t $@; fi

analyze: clean
	$(SCAN_BUILD) $(MAKE)

tidy:
	find src/ -name '*.c' \
              -a -not -name google-city-hash.c \
              -a -not -name sqlite3.c \
              -a -not -name toml.c \
              -a -not -name sha256.c \
        | xargs -n1 bash scripts/tidy-code.sh
	find pddl/ -name '*.h' \
              -a -not -name google-city-hash.c \
              -a -not -name sqlite3.c \
              -a -not -name toml.c \
              -a -not -name sha256.c \
        | xargs -n1 bash scripts/tidy-code.sh

list-global-symbols: libpddl.a
	readelf -s libpddl.a \
        | grep GLOBAL \
        | awk '{print $$8}' \
        | sort \
        | uniq \
        | grep -v '^pddl' \
        | grep -v '^_pddl' \
        | grep -v '^__pddl' \
        | grep -v '^_Z.*Ilo' \
        | grep -v '^_Z.*Ilo' \
        | grep -v '^_Z.*dynet' \
        | grep -v '^CPX' \
        | grep -v '^GRB' \
        | grep -v '^glp_' \
        | grep -v '^bliss_' \
        | grep -v '^Cudd_' \
        | less

third-party: bliss cudd
third-party-clean: bliss-clean cudd-clean

bliss: third-party/bliss/libbliss.a
bliss-clean:
	$(MAKE) -C third-party/bliss clean
	rm -f third-party/bliss/libbliss.a
	rm -f third-party/bliss/bliss_C.h
third-party/bliss/libbliss.a:
	$(MAKE) CC=$(CXX) -C third-party/bliss lib_static
	cp third-party/bliss/src/bliss_C.h third-party/bliss/
	mv third-party/bliss/libbliss_static.a $@

cudd: third-party/cudd/libcudd.a
cudd-clean:
	git clean -fdx third-party/cudd
	rm -f third-party/cudd/lib*.a
	rm -f third-party/cudd/cudd.h
third-party/cudd/libcudd.a:
	cd third-party/cudd && aclocal
	cd third-party/cudd && autoconf
	cd third-party/cudd && automake
	cd third-party/cudd && ./configure --disable-shared CC=$(CC) CXX=$(CXX)
	$(MAKE) -C third-party/cudd
	cp third-party/cudd/cudd/.libs/libcudd.a $@
	cp third-party/cudd/cudd/cudd.h third-party/cudd/cudd.h

sqlite-amalgam:
	unzip $(SQLITE_SRC_ZIP)
	mv sqlite-src-*/ sqlite
	cd sqlite/ && ./configure --disable-json --disable-load-extension
	cd sqlite/ && make OPTS="$(SQLITE_GEN_CFLAGS)" sqlite3.c
	cat sqlite/sqlite3.c | sed 's/sqlite3/pddl_sqlite3/g' >src/sqlite3.c
	cat sqlite/sqlite3.h | sed 's/sqlite3/pddl_sqlite3/g' >src/sqlite3.h
	rm -rf sqlite/

gen-pkgconfig: cpddl.pc
cpddl.pc: libpddl.a
	$(SH) ./scripts/gen-pkgconfig.sh "$(BASEPATH_)" "$(LDFLAGS)" >$@

help:
	@echo "Targets:"
	@echo "  all         - Build library (default)"
	@echo "  bin         - Bild binaries in bin/"
	@echo "  bliss       - Build Bliss library from third-party/"
	@echo "  cudd        - Bild Cudd library from third-party/"
	@echo "  third-party - Alias for 'bliss cudd'"
	@echo ""
	@echo "  clean             - Remove all generated files"
	@echo "  c                 - Like clean but does not remove sqlite object file"
	@echo "  mrproper          - Clean library and third-party/"
	@echo "  third-party-clean - Clean all third-party projects."
	@echo ""
	@echo "  check               - Run (short) automated tests"
	@echo "  check-all           - Run all automated tests"
	@echo "  check-valgrind      - Run tests with valgrind(1)"
	@echo "  check-all-valgrind"
	@echo "  check-segfault      - Run tests with valgrind(1) set up to detect only segfaults"
	@echo "  check-all-segfault"
	@echo "  check-gdb           - Run tests in gdb"
	@echo "  check-all-gdb"
	@echo ""
	@echo "  fetch-submodules - Fetch all submodules using git"
	@echo "  gen-pkgconfig - Generates pkg-config file cpddl.pc referring to this directory"
	@echo "  analyze - Static analysis with clang's scan-build (SCAN_BUILD = $(SCAN_BUILD))"
	@echo ""
	@echo "  tidy - Tidy up source code"
	@echo "  list-global-symbols - List all global symbols in libpddl.a"
	@echo "  sqlite-amalgam - Generate src/sqlite3.{c,h} from SQLite zip file defined in SQLITE_SRC_ZIP"
	@echo ""
	@echo "Variables:"
	@echo "  SYSTEM  = $(SYSTEM)"
	@echo "  CC      = $(CC)"
	@echo "  CXX     = $(CXX)"
	@echo "  SH      = $(SH)"
	@echo "  SCAN_BUILD = $(SCAN_BUILD)"
	@echo "  DEBUG   = $(DEBUG)"
	@echo "  PROFIL  = $(PROFIL)"
	@echo "  WERROR  = $(WERROR)"
	@echo "  CFLAGS  = $(CFLAGS)"
	@echo "  LDFLAGS = $(LDFLAGS)"
	@echo ""
	@echo "  LDFLAGS_EXTRA = $(LDFLAGS_EXTRA)"
	@echo "  SYSTEM_LDFLAGS = $(SYSTEM_LDFLAGS)"
	@echo ""
	@echo "  USE_BLISS         = $(USE_BLISS)"
	@echo "  BLISS_CFLAGS      = $(BLISS_CFLAGS)"
	@echo "  BLISS_LDFLAGS     = $(BLISS_LDFLAGS)"
	@echo "  USE_CUDD          = $(USE_CUDD)"
	@echo "  CUDD_CFLAGS       = $(CUDD_CFLAGS)"
	@echo "  CUDD_LDFLAGS      = $(CUDD_LDFLAGS)"
	@echo ""
	@echo "  IBM_CPLEX_ROOT    = $(IBM_CPLEX_ROOT)"
	@echo "  USE_CPLEX         = $(USE_CPLEX)"
	@echo "  CPLEX_CFLAGS      = $(CPLEX_CFLAGS)"
	@echo "  CPLEX_LDFLAGS     = $(CPLEX_LDFLAGS)"
	@echo "  GUROBI_ROOT       = $(GUROBI_ROOT)"
	@echo "  USE_GUROBI        = $(USE_GUROBI)"
	@echo "  GUROBI_CFLAGS     = $(GUROBI_CFLAGS)"
	@echo "  GUROBI_LDFLAGS    = $(GUROBI_LDFLAGS)"
	@echo "  HIGHS_ROOT        = $(HIGHS_ROOT)"
	@echo "  USE_HIGHS         = $(USE_HIGHS)"
	@echo "  HIGHS_CFLAGS      = $(HIGHS_CFLAGS)"
	@echo "  HIGHS_LDFLAGS     = $(HIGHS_LDFLAGS)"
	@echo "  LP_LDFLAGS        = $(LP_LDFLAGS)"
	@echo "  LP_CFLAGS         = $(LP_CFLAGS)"
	@echo ""
	@echo "  USE_CPOPTIMIZER      = $(USE_CPOPTIMIZER)"
	@echo "  CPOPTIMIZER_CPPFLAGS = $(CPOPTIMIZER_CPPFLAGS)"
	@echo "  CPOPTIMIZER_LDFLAGS  = $(CPOPTIMIZER_LDFLAGS)"
	@echo "  USE_MINIZINC         = $(USE_MINIZINC)"
	@echo "  MINIZINC_BIN         = $(MINIZINC_BIN)"
	@echo "  MINIZINC_VERSION     = $(MINIZINC_VERSION)"
	@echo ""
	@echo "  DYNET_ROOT        = $(DYNET_ROOT)"
	@echo "  USE_DYNET         = $(USE_DYNET)"
	@echo "  DYNET_CPPFLAGS    = $(DYNET_CPPFLAGS)"
	@echo "  DYNET_LDFLAGS     = $(DYNET_LDFLAGS)"
	@echo ""
	@echo "  USE_CLIQUER       = $(USE_CLIQUER)"
	@echo "  CLIQUER_CFLAGS    = $(CLIQUER_CFLAGS)"
	@echo "  CLIQUER_LDFLAGS   = $(CLIQUER_LDFLAGS)"

.PHONY: all bin clean help doc install analyze \
  examples mrproper \
  check check-all \
  check-valgrind check-all-valgrind \
  check-segfault check-all-segfault \
  check-gdb check-all-gdb \
  third-party third-party-clean \
  bliss bliss-clean \
  sqlite-amalgam
