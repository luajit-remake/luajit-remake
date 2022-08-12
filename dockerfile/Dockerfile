FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# install misc dependency
#
RUN apt update && apt install -y git wget tar xz-utils sudo make ninja-build python ccache g++ libtinfo-dev libz-dev lsb-release software-properties-common gnupg

# install cmake 
#
WORKDIR /opt
RUN wget -O install_cmake.sh https://github.com/Kitware/CMake/releases/download/v3.23.0/cmake-3.23.0-linux-x86_64.sh
RUN mkdir -p cmake-3.23.0 && bash install_cmake.sh --skip-license --prefix=/opt/cmake-3.23.0
RUN ln -s /opt/cmake-3.23.0/bin/* /usr/local/bin
RUN rm -f install_cmake.sh

# install llvm 14
#
RUN wget https://apt.llvm.org/llvm.sh
RUN chmod +x llvm.sh
RUN ./llvm.sh 14
RUN rm -f llvm.sh
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/clang-14 100
RUN update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-14 100
RUN ln -s /usr/lib/llvm-14/include/llvm /usr/local/include/llvm 
RUN ln -s /usr/lib/llvm-14/include/llvm-c /usr/local/include/llvm-c 
RUN ln -s /usr/lib/llvm-14/include/polly /usr/local/include/polly 

# install mold
WORKDIR /usr/local/
RUN wget -O mold.tar.gz https://github.com/rui314/mold/releases/download/v1.4.0/mold-1.4.0-x86_64-linux.tar.gz
RUN tar xf mold.tar.gz --strip-components=1
RUN rm -f mold.tar.gz
RUN update-alternatives --install /usr/bin/ld ld /usr/local/bin/mold 100
RUN update-alternatives --install /usr/bin/ld ld /usr/bin/x86_64-linux-gnu-ld 90

# set user
#
RUN useradd -ms /bin/bash u
RUN usermod -aG sudo u
RUN echo 'u ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

COPY ccache.conf /etc/ccache.conf

USER u
WORKDIR /home/u



