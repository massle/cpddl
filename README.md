# cpddl

**cpddl** is a library and a set of programs for PDDL-based automated planning
written in C.

[[_TOC_]]

## License

cpddl is licensed under OSI-approved 3-clause
[BSD License](https://opensource.org/licenses/BSD-3-Clause), text of license
is distributed along with source code in the LICENSE file.

cpddl directly incorporates several third-party works:
- The SQL library [sqlite](https://www.sqlite.org/index.html) which is in
[public-domain](https://www.sqlite.org/copyright.html);

- The [SHA256](https://github.com/B-Con/crypto-algorithms)
hash function from public-domain authored by Brad Conte;

- Two hash functions copyrighted by Google and released under the MIT
license [CityHash](https://code.google.com/p/cityhash) and
[FastHash](https://code.google.com/p/fast-hash);

- [Timsort](https://github.com/swenson/sort/) licensed under MIT
(Copyright (c) 2010-2019 Christopher Swenson, 2012 Vojtech Fried, 2012 Google Inc);

- [Toml](https://github.com/cktan/tomlc99) licensed under MIT (Copyright (c) CK Tan);


Other than that, cpddl can be compiled without any other
dependecies besides standard C-related tools.
However, certain functionalities require external libraries:
- symmetries require
[bliss](https://users.aalto.fi/~tjunttil/bliss) library licensed under LGPL
(a slightly modified copy located in the ``third-party`` directory).

- binary decision diagrams require
[cudd](https://davidkebo.com/cudd) library licensed under 3-clause BSD
License
(a copy is located in the ``third-party`` directory).

- (I)LP solver requires
[CPLEX Optimizer](https://www.ibm.com/analytics/cplex-optimizer),
[Gurobi](https://www.gurobi.com/),
[HiGHS](https://highs.dev), or
[Coin-Or](https://www.coin-or.org/). CPLEX Optimizer and
Gurobi are commercial products, but it is possible to obtain an academic
license. HiGHS is licensed under MIT license.
Coin-Or [Clp](https://github.com/coin-or/Clp/) and
[Cbc](https://github.com/coin-or/Cbc) modules are licensed under
Eclipse Public License v2.0.

  The recommended and most tested option is the CPLEX Optimizer.

- constraint optimization requires either
[CPLEX CP Optimizer](https://www.ibm.com/analytics/cplex-cp-optimizer), or
[minizinc](https://www.minizinc.org/). CPLEX CP Optimizer is a commercial
library, but it is possible to obtain an academic license. Minizinc is
licensed under Mozilla Public License v2.0 (and itself depends on other
solvers), but it is called as a subprocess from cpddl, i.e., it is never
statically or dynamically linked to cpddl.

  The recommended option is the CPLEX CP Optimizer.

## Building with Makefile

This project is built with [GNU Make](https://www.gnu.org/software/make), and
the compilation can be configured by adding a ``Makefile.config`` file with the
configuration setting to the top directory. It is recommended to start by
copying ``Makefile.config.tpl`` to ``Makefile.config`` and modify it to your
liking.

The easiest and fastest way the build the working system is by calling:
```sh
  $ cp Makefile.config.tpl Makefile.config
      # Edit Makefile.config if you need/want to.
  $ ./scripts/build.sh
```
It builds the cpddl library and binaries with [bliss](https://users.aalto.fi/~tjunttil/bliss)
and [cudd](https://davidkebo.com/cudd) libraries which are compiled from local
copies in the ``third-party/`` directory. It also uses the configuration options
placed in the ``Makefile.config`` file. ``Makefile.config.tpl`` contains
detailed instructions how to change the configuration.

Another useful commands are:
- Check the current configuration:
```sh
  $ make help
```
- Build the (static) library ``libpddl.a``:
```sh
  $ make
```
- Build the binaries in the ``bin/`` directory:
```sh
  $ make bin
```
- Remove generated/object/temporary files:
```sh
  $ make clean
```
- Remove all generated/object/temporary files including the ones in the
``third-party`` directory:
```sh
  $ make mrproper
```
- Compile all libraries from the ``third-party/`` directory:
```sh
  $ make third-party
```
- Compile the [bliss](https://users.aalto.fi/~tjunttil/bliss) library for
handling symmetries:
```sh
  $ make bliss
```
Compile the [cudd](https://davidkebo.com/cudd) library for handling binary
decision diagrams:
```sh
  $ make cudd
```

If you tried everything described above and you still cannot build the project,
then:

0. If you try to compile it on Windows, then you are out of luck. Although, it
shouldn't be problem to compile and run it, I'll not provide any support (nor
will I accept any Windows-specific patches).

1. Run ``make mrproper``.

2. Repeat all steps that you used for (unsuccessfully) building the project and record all
commands and all their outputs to both stdout and stderr.

3. Contact me at <danfis@danfis.cz>, describe what are you trying to achieve and where is
the problem, and attach all information gathered in step 2.


## Building Apptainer Image

The script ``scripts/build-apptainer.sh`` can be used to build
[Apptainer](https://apptainer.org/) images of the main binary program
``./bin/pddl``. It has a lot of options which are printed when the script is
called without any arguments.
Here is a list of recommendations how to use it:

1. If you don't need any dependencies, the following will build the smallest
possible image:
```sh
  $ ./scripts/build-apptainer.sh --no-bliss --no-cudd alpine
```

2. If you want to work with symmetries or binary decision diagrams (e.g., you
want to use symbolic search), use:
```sh
  $ ./scripts/build-apptainer.sh alpine
```

3. If you want symmetries, binary decision diagrams, and LP/MIP/CSP CPLEX
solver, then download the installation binary for CPLEX to the location, say,
``/opt/cplex/cplex_studio2211.linux_x86_64.bin`` and call:
```sh
  $ ./scripts/build-apptainer.sh --cplex /opt/cplex/cplex_studio2211.linux_x86_64.bin photon
```

4. If you want the same as above but with the HiGHS LP/MIP solver and Minizinc
as the CSP solver, call:
```sh
  $ ./scripts/build-apptainer.sh --highs --minizinc alpine
```


## References
The inference of **fact-alternating mutex groups** (``pddl/famgroup.h``) is
described in
 - Daniel Fišer, Antonín Komenda.
Fact-Alternating Mutex Groups for Classical Planning,
JAIR 61: 475-521 (2018)

The inference of **lifted fact-alternating mutex groups**
(``pddl/lifted_mgroup*.h``) is described in
- Daniel Fišer.
Lifted Fact-Alternating Mutex Groups and Pruned Grounding of Classical
Planning Problems, AAAI 2020

Pruning of unreachable and dead-end operators on the PDDL level
(``pddl/compile_in_lifted_mgroup.h``) is described in
- Daniel Fišer.
Operator Pruning using Lifted Mutex Groups via Compilation on Lifted Level,
ICAPS 2023

**Operator mutexes** (``pddl/op_mutex*.h``) are described in
 - Daniel Fišer, Álvaro Torralba, Alexander Shleyfman.
Operator Mutexes and Symmetries for Simplifying Planning Tasks,
AAAI 2019, 7586-7593

**Multi-fact disambiguations** and potential heuristics strenghtened with
disambiguations (``pddl/{pot.h,hpot.h,disambiguation.h}``) are described in
 - Daniel Fišer, Rostislav Horčík, Antonín Komenda.
Strengthening Potential Heuristics with Mutexes and Disambiguations,
ICAPS 2020

**Endomorphisms/Homomorphism**
(``pddl/endomorphism.h``, ``pddl/homomorphism*.h``) are described in
 - Rostislav Horčík, Daniel Fišer, Álvaro Torralba
Homomorphisms of Lifted Planning Tasks: The Case for Delete-free Relaxation Heuristics,
AAAI 2022
 - Rostislav Horčík, Daniel Fišer.
Endomorphisms of Classical Planning Tasks,
AAAI 2021
 - Rostislav Horčík, Daniel Fišer.
Endomorphisms of Lifted Planning Problems,
ICAPS 2021

**Custom-design FDR encodings** (``pddl/red_black_fdr.h``)
 - Daniel Fišer, Daniel Gnad, Michael Katz, Jörg Hoffmann
Custom-Design of FDR Encodings: The Case of Red-Black Planning,
IJCAI 2021

**Symbolic search** (``pddl/symbolic*.h``)
 - Daniel Fišer, Álvaro Torralba, Jörg Hoffmann.
Operator-Potentials in Symbolic Search: From Forward to Bi-Directional Search,
ICAPS 2022
 - Daniel Fišer, Álvaro Torralba, Jörg Hoffmann.
Operator-Potential Heuristics for Symbolic Search,
AAAI 2022

Please refer to these papers when documenting work that uses the corresponding
parts of cpddl.

