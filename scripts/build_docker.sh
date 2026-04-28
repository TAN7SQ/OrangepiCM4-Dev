#!/bin/bash

# 只要有一步出错立刻整个脚本退出执行
set -e

IMAGE_NAME=cm4-camera-builder:22.04

echo "[1/3] Building Docker image..."
docker build -t ${IMAGE_NAME} .

echo "[2/3] Building ARM64 executable inside Docker..."

# --rm 容器使用完就删除
# --user 使用当前用户的权限运行
# -v "$PWD":/work  把当前目录挂载到容器的 /work
# -w /work 容器启动后的工作目录就说/work
# 然后执行以下bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD":/work \
  -w /work \
  ${IMAGE_NAME} \
  bash -c "
    rm -rf build
    cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-linux-gnu.cmake
    cmake --build build -j
  "
  # 上面这个是标准cmake，
  # 先删除旧的build
  # 用CMake配置项目
  # 执行交叉编译toolchains/aarch64-linux-gnu.cmake
  # 用ninja编译

echo "[3/3] Checking output file..."
# 输出这个可执行文件的架构
file build/hello

echo "✔️Build finished."