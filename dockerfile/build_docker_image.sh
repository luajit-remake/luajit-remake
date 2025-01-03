#!/bin/bash

set -e

cd /

# install misc dependency
#
apt update
apt install -y git wget tar xz-utils sudo make ninja-build python ccache libtinfo-dev libz-dev lsb-release software-properties-common gnupg libstdc++-10-dev binutils-gold

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

# For now, do not use mold because it seems to have a bug when program is compiled with no-pie that causes dlsym() to crash
#
# install mold
#
#cd /usr/local/
#wget -O mold.tar.gz https://github.com/rui314/mold/releases/download/v1.4.0/mold-1.4.0-x86_64-linux.tar.gz
#tar xf mold.tar.gz --strip-components=1
#rm -f mold.tar.gz

update-alternatives --install /usr/bin/ld ld /usr/bin/ld.gold 120
#update-alternatives --install /usr/bin/ld ld /usr/local/bin/mold 100
update-alternatives --install /usr/bin/ld ld /usr/bin/x86_64-linux-gnu-ld 90

# Checkout LLVM 19.1.6
#
LLVM_SRC_DIR=/llvm-src
mkdir $LLVM_SRC_DIR
cd $LLVM_SRC_DIR
git clone -b llvmorg-19.1.6 --depth 1 https://github.com/llvm/llvm-project.git

cd $LLVM_SRC_DIR/llvm-project
mv /llvm.patch llvm.patch
mv /llvm_revert_a1b78fb.patch llvm_revert_a1b78fb.patch
mv /llvm_slow_3ops_lea_option.patch llvm_slow_3ops_lea_option.patch

# Apply our LLVM patches
# The option to annotate indirect branch targets in assembly, and hacks for calling conventions
#
git apply llvm.patch
# LLVM commit a1b78fb needs to reverted because it introduces bugs that 
# causes __builtin_expect to not work correctly: https://github.com/llvm/llvm-project/issues/121105
#
git apply llvm_revert_a1b78fb.patch
# Add an option to disable TuningSlow3OpsLEA
#
git apply llvm_slow_3ops_lea_option.patch

# Build and install Clang+LLVM
# Since we are already building Clang+LLVM by ourselves, take this chance to enable RTTI. 
# (otherwise we would need to either disable RTTI for our own LLVM code, or risk random link failures..)
# Clang/LLVM's performance isn't a problem, since we are only using them for the build step.
#
# We do not enable debug info for LLVM library. While seemingly good, it turns out to be a terrible idea: 
# gdb becomes extraordinarily slow due to the extra debug info, and it turns out that even LLVM's 
# stack trace printer fails due to OOM while parsing the debug info...
#
mkdir build
cd $LLVM_SRC_DIR/llvm-project/build
CC=clang-12 CXX=clang++-12 cmake -GNinja -DLLVM_ENABLE_DUMP=ON -DLLVM_ENABLE_RTTI=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang ../llvm
# Leave two CPUs idle so the system won't be irresponsible during the build
#
CC=clang-12 CXX=clang++-12 REQUIRES_RTTI=1 ninja -j$((`nproc`-2))
CC=clang-12 CXX=clang++-12 REQUIRES_RTTI=1 ninja install

# Having built Clang+LLVM, we can now uninstall the system Clang compiler
#
apt remove -y clang-12
apt autoremove -y

# It seems like after uninstalling the system Clang, the ld link is broken.. fix it
#
#update-alternatives --install /usr/bin/ld ld /usr/local/bin/mold 100
update-alternatives --install /usr/bin/ld ld /usr/bin/ld.gold 120

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

