#!/bin/bash

# ===== Docker =====
IMAGE_NAME=cm4-camera-builder:22.04

# ===== CMake / Build =====
APP_NAME=camera_ctl

BUILD_DIR=build
CMAKE_GENERATOR=Ninja
TOOLCHAIN_FILE=toolchains/aarch64-linux-gnu.cmake

# 可以扩展，比如以后加 OpenCV
CMAKE_OPTIONS="
-DCMAKE_BUILD_TYPE=Release
"

# ===== Orange Pi CM4 =====
BOARD_USER=orangepi
BOARD_IP=192.168.1.140
REMOTE_DIR=/home/orangepi/App

# ===== Runtime =====
LOG_FILE=${REMOTE_DIR}/${APP_NAME}.log