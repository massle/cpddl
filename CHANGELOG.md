# Changelog

## Unreleased

### Added
- Script for building Apptainer images
- Script for generating pkg-config file
- Support for Coin-Or MIP/LP solver
- Dynamic loading of CPLEX and Gurobi libraries during runtime
- Binaries can list available LP and CP solvers

### Changed
- Algorithm for compiling away negative conditions: Now, it adds only the
  (potentially) relevant `NOT-*` facts to the initial state; and it can be
  configured via `pddl_config_t`.
- Unified API for grounding

### Removed
- Suport for GLPK solver

### Fixed
- Fixed h^2 pruning of goal facts which simply removed the goal facts. Now,
  the task is marked as unsolvable.
