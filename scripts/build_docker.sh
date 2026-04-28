#!/bin/bash
set -e

# 加载配置
source "$(dirname "$0")/config.sh"

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
    rm -rf ${BUILD_DIR}
    cmake -S . -B ${BUILD_DIR} -G ${CMAKE_GENERATOR} \
      -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE} \
      ${CMAKE_OPTIONS}
    cmake --build ${BUILD_DIR} -j
  "

echo -e "${PURPLE}🔍 [3/3] Checking output file...${NC}"
file ${BUILD_DIR}/${APP_NAME}

echo -e "${GREEN}✅ Build finished successfully.${NC}"