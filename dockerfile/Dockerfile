FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# install misc dependency
#
RUN apt update && apt install -y git wget tar xz-utils sudo make ninja-build python ccache libtinfo-dev libz-dev lsb-release software-properties-common gnupg libstdc++-10-dev

# install cmake 
#
WORKDIR /opt
RUN wget -O install_cmake.sh https://github.com/Kitware/CMake/releases/download/v3.23.0/cmake-3.23.0-linux-x86_64.sh
RUN mkdir -p cmake-3.23.0 && bash install_cmake.sh --skip-license --prefix=/opt/cmake-3.23.0
RUN ln -s /opt/cmake-3.23.0/bin/* /usr/local/bin
RUN rm -f install_cmake.sh

# install llvm 14
#
ARG LLVM_VERSION=14
RUN wget https://apt.llvm.org/llvm.sh
RUN chmod +x llvm.sh
RUN ./llvm.sh $LLVM_VERSION all
RUN rm -f llvm.sh

RUN update-alternatives                                                                                \
        --install /usr/bin/llvm-config       llvm-config      /usr/bin/llvm-config-$LLVM_VERSION 100   \
        --slave   /usr/bin/llvm-ar           llvm-ar          /usr/bin/llvm-ar-$LLVM_VERSION           \
        --slave   /usr/bin/llvm-as           llvm-as          /usr/bin/llvm-as-$LLVM_VERSION           \
        --slave   /usr/bin/llvm-bcanalyzer   llvm-bcanalyzer  /usr/bin/llvm-bcanalyzer-$LLVM_VERSION   \
        --slave   /usr/bin/llvm-cov          llvm-cov         /usr/bin/llvm-cov-$LLVM_VERSION          \
        --slave   /usr/bin/llvm-diff         llvm-diff        /usr/bin/llvm-diff-$LLVM_VERSION         \
        --slave   /usr/bin/llvm-dis          llvm-dis         /usr/bin/llvm-dis-$LLVM_VERSION          \
        --slave   /usr/bin/llvm-dwarfdump    llvm-dwarfdump   /usr/bin/llvm-dwarfdump-$LLVM_VERSION    \
        --slave   /usr/bin/llvm-extract      llvm-extract     /usr/bin/llvm-extract-$LLVM_VERSION      \
        --slave   /usr/bin/llvm-link         llvm-link        /usr/bin/llvm-link-$LLVM_VERSION         \
        --slave   /usr/bin/llvm-mc           llvm-mc          /usr/bin/llvm-mc-$LLVM_VERSION           \
        --slave   /usr/bin/llvm-mcmarkup     llvm-mcmarkup    /usr/bin/llvm-mcmarkup-$LLVM_VERSION     \
        --slave   /usr/bin/llvm-nm           llvm-nm          /usr/bin/llvm-nm-$LLVM_VERSION           \
        --slave   /usr/bin/llvm-objdump      llvm-objdump     /usr/bin/llvm-objdump-$LLVM_VERSION      \
        --slave   /usr/bin/llvm-ranlib       llvm-ranlib      /usr/bin/llvm-ranlib-$LLVM_VERSION       \
        --slave   /usr/bin/llvm-readobj      llvm-readobj     /usr/bin/llvm-readobj-$LLVM_VERSION      \
        --slave   /usr/bin/llvm-rtdyld       llvm-rtdyld      /usr/bin/llvm-rtdyld-$LLVM_VERSION       \
        --slave   /usr/bin/llvm-size         llvm-size        /usr/bin/llvm-size-$LLVM_VERSION         \
        --slave   /usr/bin/llvm-stress       llvm-stress      /usr/bin/llvm-stress-$LLVM_VERSION       \
        --slave   /usr/bin/llvm-symbolizer   llvm-symbolizer  /usr/bin/llvm-symbolizer-$LLVM_VERSION   \
        --slave   /usr/bin/llvm-tblgen       llvm-tblgen      /usr/bin/llvm-tblgen-$LLVM_VERSION

RUN update-alternatives                                                                                             \
        --install /usr/bin/clang                 clang                 /usr/bin/clang-$LLVM_VERSION 100             \
        --slave   /usr/bin/clang++               clang++               /usr/bin/clang++-$LLVM_VERSION               \
        --slave   /usr/bin/asan_symbolize        asan_symbolize        /usr/bin/asan_symbolize-$LLVM_VERSION        \
        --slave   /usr/bin/c-index-test          c-index-test          /usr/bin/c-index-test-$LLVM_VERSION          \
        --slave   /usr/bin/clang-check           clang-check           /usr/bin/clang-check-$LLVM_VERSION           \
        --slave   /usr/bin/clang-cl              clang-cl              /usr/bin/clang-cl-$LLVM_VERSION              \
        --slave   /usr/bin/clang-cpp             clang-cpp             /usr/bin/clang-cpp-$LLVM_VERSION             \
        --slave   /usr/bin/clang-format          clang-format          /usr/bin/clang-format-$LLVM_VERSION          \
        --slave   /usr/bin/clang-format-diff     clang-format-diff     /usr/bin/clang-format-diff-$LLVM_VERSION     \
        --slave   /usr/bin/clang-import-test     clang-import-test     /usr/bin/clang-import-test-$LLVM_VERSION     \
        --slave   /usr/bin/clang-include-fixer   clang-include-fixer   /usr/bin/clang-include-fixer-$LLVM_VERSION   \
        --slave   /usr/bin/clang-offload-bundler clang-offload-bundler /usr/bin/clang-offload-bundler-$LLVM_VERSION \
        --slave   /usr/bin/clang-query           clang-query           /usr/bin/clang-query-$LLVM_VERSION           \
        --slave   /usr/bin/clang-rename          clang-rename          /usr/bin/clang-rename-$LLVM_VERSION          \
        --slave   /usr/bin/clang-reorder-fields  clang-reorder-fields  /usr/bin/clang-reorder-fields-$LLVM_VERSION  \
        --slave   /usr/bin/clang-tidy            clang-tidy            /usr/bin/clang-tidy-$LLVM_VERSION            \
        --slave   /usr/bin/lldb                  lldb                  /usr/bin/lldb-$LLVM_VERSION                  \
        --slave   /usr/bin/lldb-server           lldb-server           /usr/bin/lldb-server-$LLVM_VERSION


RUN ln -s /usr/lib/llvm-$LLVM_VERSION/include/* /usr/local/include/

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



