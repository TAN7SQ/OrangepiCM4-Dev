#!/bin/bash
set -e

IMAGE_NAME=cm4-camera-builder:22.04
APP_NAME=hello

GREEN="\033[1;32m"
BLUE="\033[1;34m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
PURPLE="\033[1;35m"
NC="\033[0m"

trap 'echo -e "${RED}❌ Build failed! Script stopped.${NC}"' ERR

echo -e "${BLUE}🐳 [1/3] Building Docker image...${NC}"
docker build -t ${IMAGE_NAME} .

echo -e "${YELLOW}🔧 [2/3] Building ARM64 executable inside Docker...${NC}"
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

echo -e "${PURPLE}🔍 [3/3] Checking output file...${NC}"
file build/${APP_NAME}

echo -e "${GREEN}✅ Build finished successfully.${NC}"