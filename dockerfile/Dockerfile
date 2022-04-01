FROM ubuntu:18.04

# install misc dependency
#
RUN apt update && apt install -y git wget tar xz-utils sudo make ninja-build python ccache g++ libtinfo-dev libz-dev

# install cmake 
#
WORKDIR /opt
RUN wget -O install_cmake.sh https://github.com/Kitware/CMake/releases/download/v3.23.0/cmake-3.23.0-linux-x86_64.sh
RUN mkdir -p cmake-3.23.0 && bash install_cmake.sh --skip-license --prefix=/opt/cmake-3.23.0
RUN ln -s /opt/cmake-3.23.0/bin/* /usr/local/bin
RUN rm -f install_cmake.sh

# install llvm 13.0.1
#
WORKDIR /usr/local/
RUN wget -O llvm.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.1/clang+llvm-13.0.1-x86_64-linux-gnu-ubuntu-18.04.tar.xz
RUN tar xf llvm.tar.xz --strip-components=1
RUN rm -f llvm.tar.xz

# set user
#
RUN useradd -ms /bin/bash u
RUN usermod -aG sudo u
RUN echo 'u ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

COPY ccache.conf /etc/ccache.conf

USER u
WORKDIR /home/u



