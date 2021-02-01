#!/bin/bash
#SBATCH -p cpufast # partition (queue)
#SBATCH -N 1 # number of nodes
#SBATCH -n 8 # number of cores
#SBATCH -x n33 # exclude node n33
#SBATCH --mem 1G # memory pool for all cores
#SBATCH -t 0-2:00 # time (D-HH:MM)
#SBATCH -o build.%N.%j.out # STDOUT
#SBATCH -e build.%N.%j.err # STDERR

# This script builds the project on the RCI cluster.
# Submit this task from the top directory of this repository as follows:
# sbatch scripts/build-rci.sh

set -x

module load GCCcore/9.3.0
module load Autotools/20180311-GCCcore-9.3.0

NCPUS=8

CPLEX_ROOT=/mnt/appl/software/CPLEX/12.9-foss-2018b

cat >Makefile.local <<EOF
CFLAGS = -march=native
CPLEX_CFLAGS = -I$CPLEX_ROOT/cplex/include
CPLEX_LDFLAGS = -L$CPLEX_ROOT/cplex/bin/x86-64_linux -Wl,-rpath=$CPLEX_ROOT/cplex/bin/x86-64_linux -lcplex1290
CPOPTIMIZER_CPPFLAGS = -I$CPLEX_ROOT/cpoptimizer/include -I$CPLEX_ROOT/concert/include/ -I$CPLEX_ROOT/cplex/include/
CPOPTIMIZER_LDFLAGS = -L$CPLEX_ROOT/cpoptimizer/lib/x86-64_linux/static_pic/ -lcp -L$CPLEX_ROOT/concert/lib/x86-64_linux/static_pic/ -lconcert -lstdc++
EOF


make mrproper
make -j$NCPUS boruvka
make -j$NCPUS opts
make -j$NCPUS bliss
make -j$NCPUS cudd
make -j$NCPUS sqlite
make -j$NCPUS
make -j$NCPUS -C bin
