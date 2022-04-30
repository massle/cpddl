#!/bin/bash

make mrproper
make third-party
make
make -C bin
