# cpddl

**cpddl** is a library for automated planning.

## License

cpddl is licensed under OSI-approved 3-clause BSD License, text of license
is distributed along with source code in BSD-LICENSE file.
Each file should include license notice, the rest should be considered as
licensed under 3-clause BSD License.

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
