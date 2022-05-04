#!/bin/bash

function expcheck() {
    topdir="$1"
    echo "DIR: ${topdir}"
    num_tasks=$(find "$topdir" -maxdepth 1 -type d -name '[0-9][0-9]*' | wc -l)
    num_finished=$(find "$topdir" -name task.finished | wc -l)
    num_timeout=$(find "$topdir" -name task.timeout | wc -l)
    num_memout=$(find "$topdir" -name task.memout | wc -l)
    num_segfault=$(find "$topdir" -name task.segfault | wc -l)
    echo "Num tasks: ${num_tasks}"
    echo "Finished: ${num_finished}"
    echo "Timeout: ${num_timeout}"
    echo "Memory out: ${num_memout}"
    echo "Segfaults: ${num_segfault}"

    echo "Exit status:"
    find "$topdir" -name task.status -exec cat '{}' ';' -exec echo ';' | sort | uniq -c

    echo
}

while [ "$1" != "" ]; do
    expcheck "$1"
    shift
done
