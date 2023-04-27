# Changelog

## Unreleased

### Added
- Script for building Apptainer images
- Script for generating pkg-config file
- Support for Coin-Or MIP/LP solver
- Dynamic loading of CPLEX and Gurobi libraries during runtime

### Changed

### Removed
- Suport for GLPK solver

### Fixed
- Fixed h^2 pruning of goal facts which simply removed the goal facts. Now,
  the task is marked as unsolvable.
