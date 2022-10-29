#!/bin/bash

set -e

cd /

# install misc dependency
#
apt update
apt install -y git wget tar xz-utils sudo make ninja-build python ccache libtinfo-dev libz-dev lsb-release software-properties-common gnupg libstdc++-10-dev

# install cmake 
#
cd /opt
wget -O install_cmake.sh https://github.com/Kitware/CMake/releases/download/v3.23.0/cmake-3.23.0-linux-x86_64.sh
mkdir -p cmake-3.23.0 && bash install_cmake.sh --skip-license --prefix=/opt/cmake-3.23.0
ln -s /opt/cmake-3.23.0/bin/* /usr/local/bin
rm -f install_cmake.sh

# We have a simple patch to workaround a bug (or feature?) in LLVM's PreserveMost calling convention, 
# and to extend LLVM's GHC calling convention to allow passing more arguments.
#
# This unfortunately means that we have to build Clang+LLVM from source (we need to build
# Clang from source as well, since we have C++ code that uses PreserveMost calling convention)
#
# Install clang-12, which will be used to build Clang+LLVM from source
# Note that thanks to clang-12 is not the default Clang version on Ubuntu, all the executable files are suffixed
# (e.g., the Clang executable is clang-12, not clang). This is a good thing for us since it happens to also 
# prevent name collision between the system version and our build-from-source version.
#
apt install -y clang-12

# install mold
#
cd /usr/local/
wget -O mold.tar.gz https://github.com/rui314/mold/releases/download/v1.4.0/mold-1.4.0-x86_64-linux.tar.gz
tar xf mold.tar.gz --strip-components=1
rm -f mold.tar.gz
update-alternatives --install /usr/bin/ld ld /usr/local/bin/mold 100
update-alternatives --install /usr/bin/ld ld /usr/bin/x86_64-linux-gnu-ld 90

# Checkout LLVM 15.0.3
#
LLVM_SRC_DIR=/llvm-src
mkdir $LLVM_SRC_DIR
cd $LLVM_SRC_DIR
git clone -b llvmorg-15.0.3 --depth 1 https://github.com/llvm/llvm-project.git

# Apply our patch
#
cd $LLVM_SRC_DIR/llvm-project
mv /llvm.patch llvm.patch
git apply llvm.patch

# Build and install Clang+LLVM
# Since we are already building Clang+LLVM by ourselves, take this chance to enable RTTI and Debug info. 
# (otherwise we would need to either disable RTTI for our own LLVM code, or risk random link failures..)
# Clang/LLVM's performance isn't a problem, since we are only using them for the build step.
#
mkdir build
cd $LLVM_SRC_DIR/llvm-project/build
CC=clang-12 CXX=clang++-12 cmake -GNinja -DLLVM_ENABLE_DUMP=ON -DLLVM_ENABLE_RTTI=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLVM_ENABLE_PROJECTS=clang ../llvm
# Leave two CPUs idle so the system won't be irresponsible during the build
#
REQUIRES_RTTI=1 ninja -j$((`nproc`-2))
# We don't need debug info for the binary files: we only need them for the LLVM library
# These debug info are huge (60GB..) so strip them now. 
# Note that the 'bin' directory contains non-binary files, so strip will return a non-zero result. 
#This is expected, so just supress it
#
strip -g bin/* || true
REQUIRES_RTTI=1 ninja install

# Having built Clang+LLVM, we can now uninstall the system Clang compiler
#
apt remove -y clang-12
apt autoremove -y

# It seems like after uninstalling the system Clang, the ld link is broken.. fix it
#
update-alternatives --install /usr/bin/ld ld /usr/local/bin/mold 100

# Remove the Clang/LLVM build directory
#
cd /
rm -rf $LLVM_SRC_DIR

# set user
#
useradd -ms /bin/bash u
usermod -aG sudo u
echo 'u ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

mv /ccache.conf /etc/ccache.conf

