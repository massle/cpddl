#DEBUG = yes
#PROFIL = yes
CPLEX_CFLAGS = -I/opt/cplex1271/cplex/include
CPLEX_LDFLAGS = -L/opt/cplex1271/cplex/lib/x86-64_linux/static_pic/ -lcplex
GUROBI_CFLAGS = -I/opt/gurobi/include
GUROBI_LDFLAGS = -L/opt/gurobi/lib -Wl,-rpath=/opt/gurobi/lib -lgurobi70
CPOPTIMIZER_CPPFLAGS = -I/opt/cplex/v12.10/cpoptimizer/include -I/opt/cplex/v12.10/concert/include/ -I/opt/cplex/v12.10/cplex/include
CPOPTIMIZER_LDFLAGS = -L/opt/cplex/v12.10/cpoptimizer/lib/x86-64_linux/static_pic/ -lcp -L/opt/cplex/v12.10/concert/lib/x86-64_linux/static_pic/ -lconcert -lstdc++
