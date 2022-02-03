-include ../Makefile.local
-include ../Makefile.include

CFLAGS += -I../
CFLAGS += $(BORUVKA_CFLAGS)
LDFLAGS += -L../ -lpddl
LDFLAGS += $(BORUVKA_LDFLAGS)
LDFLAGS += $(CPOPTIMIZER_LDFLAGS)
LDFLAGS += $(LP_LDFLAGS)
LDFLAGS += $(BLISS_LDFLAGS)
LDFLAGS += $(CLIQUER_LDFLAGS)
LDFLAGS += $(CUDD_LDFLAGS)
LDFLAGS += $(SQLITE_LDFLAGS)
LDFLAGS += -pthread -lrt -lm

ifeq '$(BIN_STATIC)' 'yes'
  LDFLAGS += -static
endif

TARGETS = pddl-check
TARGETS += pddl-strips
TARGETS += pddl-strips-as-py
TARGETS += pddl-pddl
TARGETS += pddl-lifted-mgroups
TARGETS += pddl-fdr
TARGETS += pddl-pot
TARGETS += pddl-astar
TARGETS += pddl-lifted-astar
TARGETS += pddl-lifted-gbfs
TARGETS += pddl-mgroups
TARGETS += pddl-reversibility
TARGETS += pddl-op-mutex
TARGETS += pddl-symba
TARGETS += pddl

OBJS  = opts.o
OBJS += options.o
OBJS += process_strips.o
OBJS += report_lifted_mgroups.o

all: $(TARGETS)

pddl: main.c ../libpddl.a $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) $(LDFLAGS)
pddl-lifted-gbfs: pddl-lifted-astar
	ln -f $< $@
pddl-%: %.c ../libpddl.a
	$(CC) $(CFLAGS) $(OPTS_CFLAGS) -o $@ $< $(LDFLAGS) $(OPTS_LDFLAGS)

report_%.o: report_%.c report.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGETS)
	rm -f *.o

.PHONY: all
