#!/bin/bash

make mrproper
make boruvka
make opts
make bliss
make
make -C bin
