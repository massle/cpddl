# Changelog

## Version 1.1

### Added
- Script for building Apptainer images
- Script for generating pkg-config file
- Support for Coin-Or MIP/LP solver
- Dynamic loading of CPLEX and Gurobi libraries during runtime
- Binaries can list available LP and CP solvers
- Greedy best-first search algorithm

### Changed
- Algorithm for compiling away negative conditions: Now, it adds only the
  (potentially) relevant `NOT-*` facts to the initial state; and it can be
  configured via `pddl_config_t`.
- Unified API for grounding
- Unified A\*, Lazy, and GBFS search algorithms
- Parsing of PDDL files completely re-worked with the lemon parser; it now
  provides much nicer and more helpful error messages.

### Removed
- Suport for GLPK solver

### Fixed
- Fixed h^2 pruning of goal facts which simply removed the goal facts. Now,
  the task is marked as unsolvable.

## Unreleased

### Added

### Changed

### Removed

### Fixed
