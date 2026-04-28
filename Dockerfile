FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    rsync \
    openssh-client \
    file \
    vim \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work