-include ../Makefile.local
-include ../Makefile.include

CFLAGS += -I../
LDFLAGS += -L../ -lpddl
LDFLAGS += $(CPOPTIMIZER_LDFLAGS)
LDFLAGS += $(LP_LDFLAGS)
LDFLAGS += $(BLISS_LDFLAGS)
LDFLAGS += $(CLIQUER_LDFLAGS)
LDFLAGS += $(CUDD_LDFLAGS)
LDFLAGS += -pthread -lrt -lm

ifeq '$(BIN_STATIC)' 'yes'
  LDFLAGS += -static
endif

TARGETS = pddl
TARGETS += pddl-fdr
TARGETS += pddl-symba

OBJS  = opts.o
OBJS += options.o
OBJS += process_strips.o
OBJS += report_lifted_mgroups.o
OBJS += report_mgroups.o
OBJS += lifted_planner.o

all: $(TARGETS)

pddl: main.c ../libpddl.a $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) $(LDFLAGS)
pddl-fdr: main.c ../libpddl.a $(OBJS)
	$(CC) $(CFLAGS) -DBIN_PDDL_FDR=1 -o $@ $< $(OBJS) $(LDFLAGS)
pddl-symba: main.c ../libpddl.a $(OBJS)
	$(CC) $(CFLAGS) -DBIN_PDDL_SYMBA=1 -o $@ $< $(OBJS) $(LDFLAGS)

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
