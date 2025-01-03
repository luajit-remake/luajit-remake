FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /

COPY llvm.patch /llvm.patch
COPY llvm_revert_a1b78fb.patch /llvm_revert_a1b78fb.patch
COPY llvm_slow_3ops_lea_option.patch /llvm_slow_3ops_lea_option.patch
COPY ccache.conf /ccache.conf
COPY build_docker_image.sh /build_docker_image.sh
RUN bash build_docker_image.sh
RUN rm /build_docker_image.sh

USER u
WORKDIR /home/u

