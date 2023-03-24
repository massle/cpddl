#!/bin/bash

HIGHS_VERSION="v1.5.1"
MINIZINC_LINK="https://github.com/MiniZinc/MiniZincIDE/releases/download/2.7.0/MiniZincIDE-2.7.0-bundle-linux-x86_64.tgz"

if [ "$1" = "" ]; then
    echo "Usage: $0 [OPTIONS] image"
    echo "    image:"
    echo "        alpine (does not support cplex)"
    echo "        photon"
    echo "        debian-{bullseye,buster,stretch}"
    echo "        ubuntu-{jammy,focal,bionic}"
    echo "        fedora"
    echo "        gcc-{11,12}"
    echo ""
    echo "    OPTIONS:"
    echo "        --no-bliss"
    echo "        --no-cudd"
    echo "        --highs"
    echo "        --cplex ibm_studio_installer"
    echo "        --gurobi (supported only with debian-bullseye)"
    echo "        --minizinc"
    echo "        --git tag/branch/sha"
    echo "        --git-dev tag/branch/sha"
    echo "        --clang (does not work for photon)"
    echo "        --werror"
    exit -1
fi

SETUP="
%setup
"

ADDITIONAL_FILES=

WERROR=
USE_GIT=
HAS_CPLEX=
HAS_HIGHS=
HAS_MINIZINC=
NO_BLISS=
NO_CUDD=
CLANG=
while true; do
    if [ "$1" = "--cplex" ]; then
        HAS_CPLEX=yes
        shift
        cplex_file="${1}"
        shift

        SETUP="$SETUP
            cp "$cplex_file" \$APPTAINER_ROOTFS/cplex.bin
            chmod +x \$APPTAINER_ROOTFS/cplex.bin
"

    elif [ "$1" = "--gurobi" ]; then
        HAS_GUROBI=yes
        shift

    elif [ "$1" = "--highs" ]; then
        HAS_HIGHS=yes
        shift
        SETUP="$SETUP
    git clone --depth 1 --branch $HIGHS_VERSION https://github.com/ERGO-Code/HiGHS.git \$APPTAINER_ROOTFS/HiGHS-src
"

    elif [ "$1" = "--minizinc" ]; then
        HAS_MINIZINC=yes
        shift
        SETUP="$SETUP
    wget $MINIZINC_LINK -O \$APPTAINER_ROOTFS/minizinc.tgz
"
        ADDITIONAL_FILES="$ADDITIONAL_FILES
    /minizinc
"

    elif [ "$1" = "--git" ]; then
        USE_GIT=yes
        shift
        GIT_REV="$1"
        shift
        SETUP="$SETUP
            git clone https://gitlab.com/danfis/cpddl \$APPTAINER_ROOTFS/cpddl
            git -C \$APPTAINER_ROOTFS/cpddl checkout -b __build__ ${GIT_REV}
"

    elif [ "$1" = "--git-dev" ]; then
        USE_GIT=yes
        shift
        GIT_REV="$1"
        shift
        git clone --depth 1 --branch ${GIT_REV} git@gitlab.com:danfis/cpddl-dev2 tmp-cpddl

        SETUP="$SETUP
            mv tmp-cpddl \$APPTAINER_ROOTFS/cpddl
"

    elif [ "$1" = "--werror" ]; then
        WERROR=yes
        shift

    elif [ "$1" = "--no-bliss" ]; then
        NO_BLISS=yes
        shift

    elif [ "$1" = "--no-cudd" ]; then
        NO_CUDD=yes
        shift

    elif [ "$1" = "--clang" ]; then
        CLANG=yes
        shift

    else
        break
    fi
done

if [ "$USE_GIT" != "yes" ]; then
    SETUP="$SETUP
    cp -r ./ \$APPTAINER_ROOTFS/cpddl
"
fi

SUFF=
if [ "$GIT_REV" != "" ]; then
    SUFF="${SUFF}-${GIT_REV}"
fi
if [ "$NO_BLISS" = "yes" ]; then
    SUFF="${SUFF}-nobliss"
fi
if [ "$NO_CUDD" = "yes" ]; then
    SUFF="${SUFF}-nocudd"
fi
if [ "$HAS_CPLEX" = "yes" ]; then
    SUFF="${SUFF}-cplex"
fi
if [ "$HAS_GUROBI" = "yes" ]; then
    SUFF="${SUFF}-gurobi"
fi
if [ "$HAS_HIGHS" = "yes" ]; then
    SUFF="${SUFF}-highs"
fi
if [ "$HAS_MINIZINC" = "yes" ]; then
    SUFF="${SUFF}-minizinc"
fi

MAKE="
    if [ -d /HiGHS-src ]; then
        cd /HiGHS-src
        mkdir build
        cd build
        cmake -DCMAKE_INSTALL_PREFIX=/HiGHS -DCMAKE_INSTALL_LIBDIR=lib -DSHARED=OFF ..
        make -j8
        make install
        cd ../..
    fi

    if [ -f /minizinc.tgz ]; then
        cd /
        if [ -f /usr/local/bin/minizinc ]; then
            rm -f minizinc.tgz
            mkdir -p /minizinc/bin
            ln -s /usr/local/bin/minizinc /minizinc/bin/minizinc
        else
            tar xf minizinc.tgz
            mv MiniZinc*/ minizinc
        fi
    fi

    if [ -f /cplex.bin ]; then
        /cplex.bin -i silent -DUSER_INSTALL_DIR=/cplex -DLICENSE_ACCEPTED=true
    fi

    cd /cpddl
    rm -f Makefile.config

    if [ -d /opt/gurobi/linux64 ]; then
        echo \"GUROBI_CFLAGS = -I/opt/gurobi/linux64/include\" >>Makefile.config
        if [ -f /opt/gurobi/linux64/lib/libgurobi95.so ]; then
            echo \"GUROBI_LDFLAGS = -L/opt/gurobi/linux64/lib -Wl,-rpath=/opt/gurobi/linux64/lib -lgurobi95\" >>Makefile.config
       else
           echo \"Cannot find gurobi library!\"
           exit -1
       fi
    fi

    [ -d /cplex ] && echo \"IBM_CPLEX_ROOT = /cplex\" >>Makefile.config
    [ -d /HiGHS ] && echo \"HIGHS_ROOT = /HiGHS\" >>Makefile.config
    [ -d /minizinc ] && echo \"MINIZINC_BIN = /minizinc/bin/minizinc\" >>Makefile.config
    [ -f /usr/bin/clang ] \\
        && echo \"CC = clang\" >>Makefile.config \\
        && echo \"CXX = clang++\" >>Makefile.config
    [ \"$WERROR\" != \"\" ] && echo \"WERROR = yes\" >>Makefile.config
    make mrproper
    make help
    [ \"$NO_BLISS\" = \"\" ] && make -j8 bliss
    [ \"$NO_CUDD\" = \"\" ] && make -j8 cudd
    make -j8
    make -j8 bin
    mv /cpddl/bin/pddl /
    strip --strip-all /pddl
"

RUN="
%runscript
    /pddl "\$@"

%labels
Name    cpddl
Authors Daniel Fiser <danfis@danfis.cz>
License BSD
"

function build_alpine(){
    local name="${1}${SUFF}"
    local base="$2"
    cat >Apptainer.${name} <<EOF
Bootstrap: docker
From: $base
Stage: build

$SETUP

%post
    apk update
    apk upgrade
    apk add make gcc g++ autoconf automake cmake git bash libstdc++
    [ "$CLANG" = "yes" ] && apk add clang
    $MAKE

Bootstrap: docker
From: $base
Stage: run

%files from build
    /pddl
    $ADDITIONAL_FILES
%post
    apk update
    apk upgrade
    apk add bash libstdc++

$RUN
EOF
    sudo apptainer build cpddl-${name}.img Apptainer.${name}
}

function build_debian(){
    local name="${1}${SUFF}"
    local base="$2"
    cat >Apptainer.${name} <<EOF
Bootstrap: docker
From: $base
Stage: build

$SETUP

%post
    export DEBIAN_FRONTEND=noninteractive
    apt update -y
    apt upgrade -y
    apt install -y make gcc g++ autoconf automake cmake git libstdc++6
    [ "$CLANG" = "yes" ] && apt install -y clang
    $MAKE

Bootstrap: docker
From: $base
Stage: run

%files from build
    /pddl
    $ADDITIONAL_FILES
%post
    export DEBIAN_FRONTEND=noninteractive
    apt update -y
    apt install -y libstdc++6
    apt autoremove -y
    apt-get clean -y
    rm -rf /var/lib/apt/lists/*
    rm -rf /var/lib/apt/

$RUN
EOF
    sudo apptainer build cpddl-${name}.img Apptainer.${name}
}

function build_fedora(){
    local name="${1}${SUFF}"
    local base="$2"
    cat >Apptainer.${name} <<EOF
Bootstrap: docker
From: $base
Stage: build

$SETUP

%post
    dnf -y update
    dnf -y install make gcc g++ autoconf automake cmake git libstdc++
    [ "$CLANG" = "yes" ] && dnf -y install clang
    $MAKE

Bootstrap: docker
From: $base
Stage: run

%files from build
    /pddl
    $ADDITIONAL_FILES
%post
    dnf -y update
    dnf -y install libstdc++
    dnf -y clean all
    rm -rf /var/lib/dnf
    rm -rf /var/lib/rpm*

$RUN
EOF
    sudo apptainer build cpddl-${name}.img Apptainer.${name}
}

function build_photon(){
    local name="${1}${SUFF}"
    local base="$2"
    cat >Apptainer.${name} <<EOF
Bootstrap: docker
From: $base
Stage: build

$SETUP

%post
    tdnf -y update
    tdnf -y install gcc glibc-devel binutils libstdc++ linux-api-headers
    tdnf -y install coreutils make autoconf automake cmake git grep gawk gzip
    $MAKE

Bootstrap: docker
From: $base
Stage: run

%files from build
    /pddl
    $ADDITIONAL_FILES
%post
    tdnf -y update
    tdnf -y install libstdc++
    tdnf -y clean all
    rm -rf /var/lib/dnf
    rm -rf /var/lib/rpm*

$RUN
EOF
    sudo apptainer build cpddl-${name}.img Apptainer.${name}
}

[ "$HAS_GUROBI" = "yes" ] \
    && [ "$1" != "debian" ] \
    && [ "$1" != "debian-bullseye" ] \
    && echo "Error: --gurobi works only with debian-bullseye" \
    && exit -1

if [ "$1" = "alpine" ]; then
    if [ "$HAS_MINIZINC" = "yes" ]; then
        build_alpine alpine minizinc/minizinc:latest-alpine
    else
        build_alpine alpine alpine:latest
    fi

elif [ "$1" = "debian" ] || [ "$1" = "debian-bullseye" ]; then
    if [ "$HAS_GUROBI" = "yes" ]; then
        build_debian debian-bullseye gurobi/optimizer:9.5.1
    else
        build_debian debian-bullseye debian:bullseye-slim
    fi
elif [ "$1" = "debian-buster" ]; then
    build_debian debian-buster debian:buster-slim
elif [ "$1" = "debian-stretch" ]; then
    build_debian debian-stretch debian:stretch-slim

elif [ "$1" = "gcc-12" ]; then
    build_debian gcc-12 gcc:12
elif [ "$1" = "gcc-11" ]; then
    build_debian gcc-11 gcc:11

elif [ "$1" = "ubuntu" ] || [ "$1" = "ubuntu-jammy" ]; then
    build_debian ubuntu-jammy ubuntu:jammy
elif [ "$1" = "ubuntu-focal" ]; then
    build_debian ubuntu-focal ubuntu:focal
elif [ "$1" = "ubuntu-bionic" ]; then
    build_debian ubuntu-bionic ubuntu:bionic

elif [ "$1" = "fedora" ]; then
    build_fedora fedora fedora:36
elif [ "$1" = "photon" ]; then
    build_photon photon photon:latest
fi
