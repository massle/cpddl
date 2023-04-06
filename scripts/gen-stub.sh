#!/bin/bash

IN_H="$1"

echo '#include "internal.h"'
echo '#include "'$IN_H'"'
echo '#define ERROR PANIC("'$2'")'
grep -Pzo '[a-z][a-zA-Z0-9_]+ [*]*pddl[A-Z][a-zA-Z0-9]*\([^;]+' "$IN_H" \
         | awk 'BEGIN { RS = "\0" }
                { printf("%s\n{ ERROR;", $0) }
                /^[^ ]+ [*]+pddl/{ printf("return NULL;") }
                /^(int|long|unsigned|float|double) pddl/{ printf("return -1;"); }
                { printf("}\n") }'

