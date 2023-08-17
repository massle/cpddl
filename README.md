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


## Usage Examples

**cpddl** is first and foremost a C library, but it also comes with several
programs that can be called from the command line. The main program is
``./bin/pddl``, but there are several other programs that implement a subset of
features of ``./bin/pddl`` and have a different default values for some options.
Running any of those programs without an argument (or with ``-h`` or ``--help``)
prints a (long) list of available options.

### Validation of PDDL Tasks
To check correctness of the input PDDL files, call
```sh
  $ ./bin/pddl --pddl-stop --pedantic domain.pddl problem.pddl
```

If there is an issue with the PDDL files, it tries to report what exactly is the
problem and where it occurs. For example, parsing the original storage domain
from IPC 2006 results in the following error message:
```
Error: The type 'area' is defined for the second time. line: 9, column: 9
/home/danfis/dev/plan/downward-benchmarks/storage/domain.pddl:9:9:
     6 | (:types hoist surface place area - object
     7 |         container depot - place
     8 |         storearea transitarea - area
     9 |         area crate - surface)
       |         ^--- here
```

### PDDL Normalization
cpddl can be used to normalize PDDL input files, i.e., as a translator that
takes input domain and problem PDDL files and outputs domain and problem PDDL
files without disjunctions, quantifiers, or (dynamic) negative conditions.
```sh
  $ ./bin/pddl --pddl-domain-out d.pddl --pddl-problem-out p.pddl --pddl-stop domain.pddl problem.pddl
```
It is also possible to compile away conditional effects (the option
``--pddl-ce``), enforce unit cost (``--pddl-unit-cost``), or more precisely
control which forms of negative conditions are compiled away
(``--pddl-neg--cond``).

It is also possible to apply pruning of unreachable or dead-end operators via
a compilation of **lifted fact-alternating mutex groups** on the PDDL level
(``--pddl-compile-in-lmg``). For description of this method, see
> Daniel Fišer.
> *Operator Pruning using Lifted Mutex Groups via Compilation on Lifted Level*,
> ICAPS 2023

Another pruning on the PDDL level available in cpddl is the method based on
**PDDL endomorphisms** (options ``--lendo`` and ``--lendo-ignore-costs``). For
details, see
> Rostislav Horčík, Daniel Fišer.
> *Endomorphisms of Lifted Planning Problems*,
> ICAPS 2021

### Lifted Planning
cpddl implements lifted planning algorithms, i.e., planning that operates
directly on the PDDL level without fully grounding the task first. Note that the
PDDL tasks are normalized first, so the options related PDDL normalization takes
effect here too.

The blind lifted A\* can be invoked by
```sh
  $ ./bin/pddl --lplan astar --lplan-out plan.out domain.pddl problem.pddl
```

The "lazy" greedy best-first search with the lifted $h^\mathrm{add}$ heuristic
described in
> Augusto B. Corrêa, Guillem Francès, Florian Pommerening and Malte Helmert.
> *Delete-Relaxation Heuristics for Lifted Classical Planning*,
> ICAPS 2021
```sh
  $ ./bin/pddl --lplan lazy --lplan-h add domain.pddl problem.pddl
```

The search with the heuristic computed in the reduced planning task using
**homomorphisms** is described in
> Rostislav Horčík, Daniel Fišer, Álvaro Torralba.
> *Homomorphisms of Lifted Planning Tasks: The Case for Delete-free Relaxation Heuristics*,
> AAAI 2022

The best-performing variant of the optimal search from this paper:
```sh
  $ ./bin/pddl --lplan astar --lplan-h homo-lmc \
               --lplan-h-homo type=rnd-objs,rm-ratio=.95,samples=50 \
               --lplan-out plan.out \
               domain.pddl problem.pddl
```

The selection of homomorphisms using **Gaifman Graphs** can be enabled by
setting `type=gaif` in the `--lplan-h-homo`option.
> Rostislav Horčík, Daniel Fišer.
> *Gaifman Graphs in Lifted Planning*,
> ECAI 2023

The best-performing variant of the optimal planner from the ECAI'23 paper:
```sh
  $ ./bin/pddl --lendo --pddl-compile-in-lmg \
               --lplan astar --lplan-h homo-lmc \
               --lplan-h-homo type=gaif,rm-ratio=.95,samples=50,sampling-max-time=60 \
               --lplan-out plan.out \
               domain.pddl problem.pddl
```


### Translation to FDR
cpddl can ground the input PDDL files and translate them into the Finite Domain
Representaition (FDR). The following command applies forward/backward $h^2$
pruning, irrelevance analysis, and few other pruning tehchniques, and it writes
the FDR encoding in the Fast Downward [format](https://www.fast-downward.org/TranslatorOutputFormat)
to the ``output.sas`` file:
```sh
  $ ./bin/pddl --h2 --fdr-out output.sas domain.pddl problem.pddl
```

By default, the translator infers *lifted fact-alternating mutex groups*
(fam-groups) according to the following paper:
> Daniel Fišer.
> *Lifted Fact-Alternating Mutex Groups and Pruned Grounding of Classical Planning Problems*,
> AAAI 2020

However, it is possible to switch to the Fast Downard (and weaker) variant of
the lifted fam-groups by using the option ``--lmg-fd``.

It is also possible to use a complete set of **fam-groups** inferred on the
ground level using the option ``--mg fam`` (this option requires a MIP solver).
> Daniel Fišer, Antonín Komenda.
> *Fact-Alternating Mutex Groups for Classical Planning*,
> JAIR 61: 475-521 (2018)

Pruning using **operator mutexes** can be applied using the option ``--P-opm``.
> Daniel Fišer, Álvaro Torralba, Alexander Shleyfman.
> *Operator Mutexes and Symmetries for Simplifying Planning Tasks*,
> AAAI 2019

For example, the following command prunes operators using operator mutexes
inferred using the so-called op-fact compilation and symmetries (requires bliss
is compiled-in):
```sh
  $ ./bin/pddl --h2 --P-opm op-fact=2,p=greedy --fdr-out output.sas domain.pddl problem.pddl
```

Pruning using **endomorphisms** on the ground level can be applied using the
``--P-endo`` option.
> Rostislav Horčík, Daniel Fišer.
> *Endomorphisms of Classical Planning Tasks*,
> AAAI 2021


Translation to the FDR targeting Red-Black planning can be turned on by the
option ``--rb-fdr``.
> Daniel Fišer, Daniel Gnad, Michael Katz, Jörg Hoffmann
> *Custom-Design of FDR Encodings: The Case of Red-Black Planning*,
> IJCAI 2021


### Potential Heuristics
cpddl also implements ground search algorithms with several heuristics.

**Potential heuristics** strengthened with **disambiguations** is described in
> Daniel Fišer, Rostislav Horčík, Antonín Komenda.
> *Strengthening Potential Heuristics with Mutexes and Disambiguations*,
> ICAPS 2020

The best-peforming variant can be run by
```sh
  $ ./bin/pddl --h2 --gplan astar --gplan-h pot --gplan-pot A+I domain.pddl problem.pddl
```


### Symbolic Search
Symbolic search using binary decision diagrams requires that cpddl is compiled
with the cudd library.

Besides blind search, we developed also so-called **operator-potential heuristics**
that significantly improve the performance.
> Daniel Fišer, Álvaro Torralba, Jörg Hoffmann.
> *Operator-Potential Heuristics for Symbolic Search*,
> AAAI 2022

> Daniel Fišer, Álvaro Torralba, Jörg Hoffmann.
> *Operator-Potentials in Symbolic Search: From Forward to Bi-Directional Search*,
> ICAPS 2022

The bi-directional symbolic search with the blind backward search and the best
variant of the operator-potential heuristic for the forward direction can be
invoked by
```sh
  $ ./bin/pddl-symba --fdr-tnfm --symba bi --symba-fw-pot --symba-fw-pot-cfg A+I \
                     domain.pddl problem.pddl
```

Please refer to the listed papers when documenting work that uses the
corresponding parts of cpddl.


### Action Schema Networks
cpddl includes implementation of Action Schema Networks (ASNets) using the
[DyNet](https://github.com/clab/dynet) library.
> Sam Toyer, Sylvie Thiébaux, Felipe Trevizan, Lexing Xie
> *ASNets: Deep Learning for Generalised Planning*,
> JAIR 2020

To enable DyNet, set ``DYNET_ROOT``, or ``DYNET_CPPFLAGS`` and ``DYNET_LDFLAGS``
variables in ``Makefile.config``.

ASNets are implemented in the ``bin/pddl-asnets`` binary. It uses configuration
files, see the [pddl-data](https://gitlab.com/danfis/pddl-data) repository,
directory ``learn-policy/deterministic``. The default configuration file can be
generated by
```sh
  $ ./bin/pddl-asnets -g config.toml
```

To train a policy while saving the current policy after each epoch with prefix
``policy``:
```sh
  $ ./bin/pddl-asnets -t output.policy /path/to/config.file --train-save-prefix policy
```

To evaluate policy:
```sh
  $ ./bin/pddl-asnets -e trained.policy /path/to/config.file
```
