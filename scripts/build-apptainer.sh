#!/bin/bash

HIGHS_VERSION="v1.5.1"
CLINGO_VERSION="v5.6.2"
MINIZINC_LINK="https://github.com/MiniZinc/MiniZincIDE/releases/download/2.7.0/MiniZincIDE-2.7.0-bundle-linux-x86_64.tgz"

if [ "$1" = "" ]; then
    echo "Usage: $0 [OPTIONS] image"
    echo "    image:"
    echo "        alpine (does not support cplex or gurobi)"
    echo "        photon"
    echo "        debian-{bookworm,bullseye,buster,stretch,testing}"
    echo "        ubuntu-{kinetic,jammy,focal,bionic}"
    echo "        fedora"
    echo "        gcc-{11,12}"
    echo ""
    echo "    OPTIONS:"
    echo "        --output filename"
    echo "        --name name-suffix"
    echo "        --no-bliss"
    echo "        --no-cudd"
    echo "        --highs"
    echo "        --clingo"
    echo "        --coin-or (supported only with debian/ubuntu/fedora)"
    echo "        --cplex ibm_studio_installer"
    echo "        --cplex-api /path/to/include/dir"
    echo "        --gurobi (supported only with debian-bullseye)"
    echo "        --minizinc"
    echo "        --git tag/branch/sha"
    echo "        --git-dev tag/branch/sha"
    echo "        --clang (does not work for photon)"
    echo "        --clang-ver version (works only for debian and ubuntu)"
    echo "        --werror"
    exit -1
fi

SETUP="
%setup
"

ADDITIONAL_FILES=

OUTPUT=
NAME=
WERROR=
USE_GIT=
HAS_CPLEX=
HAS_CPLEX_API=
HAS_GUROBI=
HAS_HIGHS=
HAS_CLINGO=
HAS_COIN_OR=
HAS_MINIZINC=
NO_BLISS=
NO_CUDD=
CLANG=
CLANG_VERSION=
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
    elif [ "$1" = "--cplex-api" ]; then
        HAS_CPLEX_API=yes
        shift
        cplex_dir="${1}"
        shift

        SETUP="$SETUP
            mkdir -p \$APPTAINER_ROOTFS/cplex/cplex/include
            cp -rv ${cplex_dir}/* \$APPTAINER_ROOTFS/cplex/cplex/include/
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

    elif [ "$1" = "--clingo" ]; then
        HAS_CLINGO=yes
        shift
        SETUP="$SETUP
    git clone --depth 1 --branch $CLINGO_VERSION https://github.com/potassco/clingo.git \$APPTAINER_ROOTFS/clingo-src
"
        ADDITIONAL_FILES="$ADDITIONAL_FILES
    /clingo/lib/libclingo.so*
"

    elif [ "$1" = "--coin-or" ]; then
        HAS_COIN_OR=yes
        shift

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
        git clone --depth 1 --branch ${GIT_REV} git@gitlab.com:danfis/cpddl-devel tmp-cpddl

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

    elif [ "$1" = "--clang-ver" ]; then
        CLANG=yes
        shift
        CLANG_VERSION=$1
        shift
        SETUP="$SETUP
            wget -O - https://apt.llvm.org/llvm.sh >\$APPTAINER_ROOTFS/llvm.sh
"

    elif [ "$1" = "--output" ]; then
        shift
        OUTPUT="$1"
        shift

    elif [ "$1" = "--name" ]; then
        shift
        NAME="$1"
        shift

    else
        break
    fi
done

if [ "$USE_GIT" != "yes" ]; then
    SETUP="$SETUP
    cp -r ./ \$APPTAINER_ROOTFS/cpddl
    git -C \$APPTAINER_ROOTFS/cpddl clean -fdx
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
if [ "$HAS_CPLEX_API" = "yes" ]; then
    SUFF="${SUFF}-cplexapi"
fi
if [ "$HAS_GUROBI" = "yes" ]; then
    SUFF="${SUFF}-gurobi"
fi
if [ "$HAS_HIGHS" = "yes" ]; then
    SUFF="${SUFF}-highs"
fi
if [ "$HAS_CLINGO" = "yes" ]; then
    SUFF="${SUFF}-clingo"
fi
if [ "$HAS_COIN_OR" = "yes" ]; then
    SUFF="${SUFF}-coinor"
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

    if [ -d /clingo-src ]; then
        cd /clingo-src
        mkdir build
        cd build
        cmake -DCMAKE_INSTALL_PREFIX=/clingo ..
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
    if [ \"$CLANG_VERSION\" != \"\" ]; then
        echo \"CC = clang-${CLANG_VERSION}\" >>Makefile.config
        echo \"CXX = clang++-${CLANG_VERSION}\" >>Makefile.config
    else
        [ -f /usr/bin/clang ] \\
            && echo \"CC = clang\" >>Makefile.config \\
            && echo \"CXX = clang++\" >>Makefile.config
    fi

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
    [ \"$HAS_CPLEX_API\" = \"yes\" ] && echo \"CPLEX_ONLY_API = yes\" >>Makefile.config
    [ -d /HiGHS ] && echo \"HIGHS_ROOT = /HiGHS\" >>Makefile.config
    [ -d /clingo ] && echo \"CLINGO_ROOT = /clingo\" >>Makefile.config
    [ -f /usr/include/coin/OsiSolverInterface.hpp ] && echo \"COIN_OR_USE_PKGCONFIG = yes\" >>Makefile.config
    [ -d /minizinc ] && echo \"MINIZINC_BIN = /minizinc/bin/minizinc\" >>Makefile.config
    [ \"$WERROR\" != \"\" ] && echo \"WERROR = yes\" >>Makefile.config
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
    [ "$NAME" != "" ] && name="$NAME"
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
    [ "$HAS_HIGHS" = "yes" ] && apk add zlib-static zlib-dev
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
    output="$OUTPUT"
    [ "$output" = "" ] && output=cpddl-${name}.sif
    sudo apptainer build "$output" Apptainer.${name}
}

function build_debian(){
    local name="${1}${SUFF}"
    [ "$NAME" != "" ] && name="$NAME"
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
    [ "$HAS_COIN_OR" = "yes" ] && apt install -y coinor-libosi-dev coinor-libclp-dev coinor-libcbc-dev zlib1g-dev pkg-config
    [ -f /llvm.sh ] \\
        && apt install -y lsb-release wget software-properties-common gnupg \\
        && bash /llvm.sh $CLANG_VERSION
    [ "$CLANG" = "yes" ] && [ ! -f /llvm.sh ] && apt install -y clang
    [ "$HAS_HIGHS" = "yes" ] && apt install -y libz-dev
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
    [ "$HAS_COIN_OR" = "yes" ] && apt install -y coinor-libclp1 coinor-libcbc3
    apt autoremove -y
    apt-get clean -y
    rm -rf /var/lib/apt/lists/*
    rm -rf /var/lib/apt/

$RUN
EOF
    output="$OUTPUT"
    [ "$output" = "" ] && output=cpddl-${name}.sif
    sudo apptainer build "$output" Apptainer.${name}
}

function build_fedora(){
    local name="${1}${SUFF}"
    [ "$NAME" != "" ] && name="$NAME"
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
    [ "$HAS_COIN_OR" = "yes" ] && dnf -y install -y coin-or-Cbc-devel coin-or-Clp-devel coin-or-Osi-devel
    [ "$HAS_HIGHS" = "yes" ] && dnf -y install zlib-devel
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
    [ "$HAS_COIN_OR" = "yes" ] && dnf -y install -y coin-or-Cbc coin-or-Clp coin-or-Osi
    dnf -y clean all
    rm -rf /var/lib/dnf
    rm -rf /var/lib/rpm*

$RUN
EOF
    output="$OUTPUT"
    [ "$output" = "" ] && output=cpddl-${name}.sif
    sudo apptainer build "$output" Apptainer.${name}
}

function build_photon(){
    local name="${1}${SUFF}"
    [ "$NAME" != "" ] && name="$NAME"
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
    [ "$HAS_HIGHS" = "yes" ] && tdnf -y install zlib-devel
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
    output="$OUTPUT"
    [ "$output" = "" ] && output=cpddl-${name}.sif
    sudo apptainer build "$output" Apptainer.${name}
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

elif [ "$1" = "debian" ] || [ "$1" = "debian-bookworm" ]; then
    build_debian debian-bookworm debian:bookworm-slim
elif [ "$1" = "debian-bullseye" ]; then
    if [ "$HAS_GUROBI" = "yes" ]; then
        build_debian debian-bullseye gurobi/optimizer:9.5.1
    else
        build_debian debian-bullseye debian:bullseye-slim
    fi
elif [ "$1" = "debian-buster" ]; then
    build_debian debian-buster debian:buster-slim
elif [ "$1" = "debian-stretch" ]; then
    build_debian debian-stretch debian:stretch-slim
elif [ "$1" = "debian-testing" ]; then
    build_debian debian-testing debian:testing-slim

elif [ "$1" = "gcc-12" ]; then
    build_debian gcc-12 gcc:12
elif [ "$1" = "gcc-11" ]; then
    build_debian gcc-11 gcc:11

elif [ "$1" = "ubuntu" ] || [ "$1" = "ubuntu-kinetic" ]; then
    build_debian ubuntu-kinetic ubuntu:kinetic
elif [ "$1" = "ubuntu-jammy" ]; then
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
