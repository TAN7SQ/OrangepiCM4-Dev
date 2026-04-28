#!/bin/bash
set -e

IMAGE_NAME=cm4-camera-builder:22.04

BOARD_USER=orangepi
BOARD_IP=192.168.1.140
REMOTE_DIR=/home/orangepi/App

APP_NAME=hello
LOG_FILE=${REMOTE_DIR}/${APP_NAME}.log

GREEN="\033[1;32m"
BLUE="\033[1;34m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
PURPLE="\033[1;35m"
NC="\033[0m"

trap 'echo -e "${RED}❌ Error occurred! Script stopped.${NC}"' ERR

echo -e "${BLUE}🐳 [1/8] Building Docker image...${NC}"
docker build -t ${IMAGE_NAME} .

echo -e "${YELLOW}🔧 [2/8] Building ARM64 executable inside Docker...${NC}"
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

echo -e "${PURPLE}🔍 [3/8] Checking output file...${NC}"
file build/${APP_NAME}

echo -e "${BLUE}🚀 [4/8] Creating remote directory...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "mkdir -p ${REMOTE_DIR}"

echo -e "${YELLOW}🛑 [5/8] Killing old process if exists...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "pgrep -x ${APP_NAME} >/dev/null && pkill -x ${APP_NAME} || true"

echo -e "${YELLOW}🧹 [6/8] Cleaning old log...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "rm -f ${LOG_FILE}"

echo -e "${BLUE}📦 [7/8] Uploading executable...${NC}"
rsync -avz build/${APP_NAME} ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/

echo -e "${GREEN}⚙️ [8/8] Starting app in background...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "chmod +x ${REMOTE_DIR}/${APP_NAME} && nohup ${REMOTE_DIR}/${APP_NAME} > ${LOG_FILE} 2>&1 &"

echo -e "${PURPLE}📜 Tailing log from CM4... Press Ctrl+C to stop watching.${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "tail -f ${LOG_FILE}" || true

echo -e "${GREEN}✅ Log watching stopped. App is still running on CM4.${NC}"