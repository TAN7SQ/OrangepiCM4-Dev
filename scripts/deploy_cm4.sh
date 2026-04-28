#!/bin/bash
set -e

# ===== 加载配置 =====
source "$(dirname "$0")/config.sh"

GREEN="\033[1;32m"
BLUE="\033[1;34m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
PURPLE="\033[1;35m"
NC="\033[0m"

trap 'echo -e "${RED}❌ Error occurred! Script stopped.${NC}"' ERR

echo -e "${BLUE}🚀 [1/6] Creating remote directory...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "mkdir -p ${REMOTE_DIR}"

echo -e "${YELLOW}🛑 [2/6] Killing old process if exists...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "pgrep -x ${APP_NAME} >/dev/null && pkill -x ${APP_NAME} || true"

echo -e "${YELLOW}🧹 [3/6] Cleaning old log...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "rm -f ${LOG_FILE}"

echo -e "${BLUE}📦 [4/6] Uploading executable...${NC}"
rsync -avz ${BUILD_DIR}/${APP_NAME} ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/

echo -e "${GREEN}⚙️ [5/6] Starting app in background...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "
chmod +x ${REMOTE_DIR}/${APP_NAME}
nohup ${REMOTE_DIR}/${APP_NAME} > ${LOG_FILE} 2>&1 &
"
echo -e "${BLUE}🔎 Checking if process is running...${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "pgrep -x ${APP_NAME} && echo '✅ Running' || echo '❌ Not running'"

echo -e "${PURPLE}📜 [6/6] Tailing log from CM4... Press Ctrl+C to stop watching.${NC}"
ssh ${BOARD_USER}@${BOARD_IP} "tail -f ${LOG_FILE}" || true

echo -e "${GREEN}✅ Log watching stopped. App is still running on CM4.${NC}"