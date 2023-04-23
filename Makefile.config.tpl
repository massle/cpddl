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
# Set to no to completely disable CPLEX library
#USE_CPLEX = no
# Set to no to completely disable CPLEX CP Optimizer library
#USE_CPOPTIMIZER = no

# Set to yes if CPLEX should not be linked. This allows to compile cpddl
# with ability to load CPLEX dynamically from .so library. Setting this to
# yes also disables CPOPTIMIZER, i.e., it is not possible to link to CPLEX
# CP Optimizer but not to CPLEX at the same time.
#CPLEX_ONLY_API = yes

# Configuration of Gurobi optimizer
#GUROBI_ROOT = /opt/gurobi951/linux64
#GUROBI_CFLAGS = -I/opt/gurobi951/linux64/include
#GUROBI_LDFLAGS = -L/opt/gurobi951/linux64/lib -Wl,-rpath=/opt/gurobi951/linux64/lib -lgurobi95
# Set to no to completely disable Gurobi library
#USE_GUROBI = no

# As CPLEX_ONLY_API but for the Gurobi library
#GUROBI_ONLY_API = yes

# Configuration of HiGHS library https://highs.dev
#HIGHS_ROOT = /opt/HiGHS
#HIGHS_CFLAGS = -I/opt/HiGHS/include
#HIGHS_LDFLAGS = -L/opt/HiGHS/lib -lhighs
# Set to no to completely disable HiGHS library
#USE_HIGHS = no

# Configuration of Coin-Or library https://www.coin-or.org/
#COIN_OR_USE_PKGCONFIG = no
#COIN_OR_CFLAGS = -I/usr/include/coin
#COIN_OR_LDFLAGS = -lCbcSolver -lCbc -lpthread -lrt -lCgl -lOsiClp -lClpSolver -lClp -lOsi -lCoinUtils -lbz2 -lz -llapack -lblas -lm
# Set to no to completely disable HiGHS library
#USE_COIN_OR = no

# Configuration of Minizinc optimizer
#MINIZINC_BIN = /opt/minizinc/bin/minizinc

# Configuration of dynet library https://github.com/clab/dynet.git
#DYNET_ROOT = /opt/dynet
#DYNET_CPPFLAGS = -I/opt/dynet/include
#DYNET_LDFLAGS = -L/opt/dynet/lib -ldynet
