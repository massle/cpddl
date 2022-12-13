# Set up specific compiler
#CC = clang
#CXX = clang++

# Turn on debug build
#DEBUG = yes

# Compile with -Werror
#WERROR = yes

# Turn on compilation with profiling
#PROFIL = yes

# Configuration of IBM CPLEX Optimization Studio
#IBM_CPLEX_ROOT = /opt/cplex/v12.10
#CPLEX_CFLAGS = -I/opt/cplex1271/cplex/include
#CPLEX_LDFLAGS = -L/opt/cplex1271/cplex/lib/x86-64_linux/static_pic/ -lcplex
#CPOPTIMIZER_CPPFLAGS = -I/opt/cplex/v12.10/cpoptimizer/include -I/opt/cplex/v12.10/concert/include/ -I/opt/cplex/v12.10/cplex/include
#CPOPTIMIZER_LDFLAGS = -L/opt/cplex/v12.10/cpoptimizer/lib/x86-64_linux/static_pic/ -lcp -L/opt/cplex/v12.10/concert/lib/x86-64_linux/static_pic/ -lconcert -lstdc++

# Configuration of Gurobi optimizer
#GUROBI_CFLAGS = -I/opt/gurobi/include
#GUROBI_LDFLAGS = -L/opt/gurobi/lib -Wl,-rpath=/opt/gurobi/lib -lgurobi70

# Configuration of HiGHS library https://highs.dev
#HIGHS_ROOT = /opt/HiGHS
#HIGHS_CFLAGS = -I/opt/HiGHS/include
#HIGHS_LDFLAGS = -L/opt/HiGHS/lib -lhighs

# Configuration of Minizinc optimizer
#MINIZINC_BIN = /opt/minizinc/bin/minizinc

# Configuration of dynet library https://github.com/clab/dynet.git
#DYNET_ROOT = /opt/dynet
#DYNET_CPPFLAGS = -I/opt/dynet/include
#DYNET_LDFLAGS = -L/opt/dynet/lib -ldynet
