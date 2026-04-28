#!/bin/bash
set -e

echo "🚀 One-click build & deploy starting..."

./scripts/build_docker.sh
./scripts/deploy_cm4.sh

