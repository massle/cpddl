# cpddl

**cpddl** is a library for automated planning.

## License

cpddl is licensed under OSI-approved 3-clause BSD License, text of license
is distributed along with source code in BSD-LICENSE file.
Each file should include license notice, the rest should be considered as
licensed under 3-clause BSD License.

Some parts of cpddl depend on other libraries such as CPLEX/Gurobi or Bliss.
So, in case you use these third-party libraries, you need to check their
respective licenses too.

## Compile

Easiest way to compile the library and the binaries that come with the
library:
```sh
  $ ./script/build.sh
```

You can change default configuration by adding Makefile.local file containing
the new configuration (see Makefile.local.tpl).

You can check the current configuration by
```sh
  $ make help
```

For example, if you want to use the parts depending on a LP solver, you need
to configure paths to CPLEX (or Gurobi) solver (see Makefile.local.tpl):
```sh
  $ echo "CPLEX_CFLAGS = -I/path/to/cplex/include" >>Makefile.local
  $ echo "CPLEX_LDFLAGS = -L/path/to/cplex/lib -lcplex" >>Makefile.local
```
And then re-build the whole library.

## References
The inference of **fact-alternating mutex groups** (pddl/famgroup.h) is
described in
 - Daniel Fišer, Antonín Komenda.
Fact-Alternating Mutex Groups for Classical Planning,
JAIR 61: 475-521 (2018)

The inference of **lifted fact-alternating mutex groups**
(pddl/lifted\_mgroup\*.h) is described in
- Daniel Fišer.
Lifted Fact-Alternating Mutex Groups and Pruned Grounding of Classical
Planning Problems, AAAI 2020

**Operator mutexes** (pddl/op\_mutex\*.h) are described in
 - Daniel Fišer, Álvaro Torralba, Alexander Shleyfman.
Operator Mutexes and Symmetries for Simplifying Planning Tasks,
AAAI 2019, 7586-7593

**Multi-fact disambiguations** and potential heuristics strenghtened with
disambiguations (pddl/{pot.h,hpot.h,disambiguation.h}) are described in
 - Daniel Fišer, Rostislav Horčík, Antonín Komenda.
Strengthening Potential Heuristics with Mutexes and Disambiguations,
ICAPS 2020

Please refer to these papers when documenting work that uses the corresponding
parts of cpddl.

