#!/bin/bash

make mrproper
make opts
make bliss
make cudd
make sqlite
make
make -C bin
