# cpddl

**cpddl** is a library for automated planning.

## License

cpddl is licensed under OSI-approved 3-clause BSD License, text of license
is distributed along with source code in BSD-LICENSE file.
Each file should include license notice, the rest should be considered as
licensed under 3-clause BSD License.

## Compile And Install

Easiest way to compile the library and the binaries that come with the
library:
```sh
  $ ./script/build.sh
```

You can change default configuration by adding Makefile.local file containing
the new configuration.

You can check the current configuration by calling:
```sh
  $ make help
```

For example, if you want to use your own installation of the library boruvka,
you can add definitions of *BORUVKA_CFLAGS* and *BORUVKA_LDFLAGS* variables
to the Makefile.local file:
```sh
  $ echo "BORUVKA_CFLAGS = -I/path/to/boruvka" >>Makefile.local
  $ echo "BORUVKA_LDFLAGS = -L/path/to/boruvka -lboruvka" >>Makefile.local
```

and then just skip compiling the local submodule of the boruvka library:
```
  $ make opts
  $ make
  $ make -C bin
```
