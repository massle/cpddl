#!/bin/bash

make mrproper
make boruvka
make opts
make bliss
make cudd
make
make -C bin
