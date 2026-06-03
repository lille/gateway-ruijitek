#!/bin/sh
set -eu

APP_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BIN_PATH="${APP_DIR}/bin/rk3568-dtu-terminal-demo"
CONFIG_PATH="${APP_DIR}/config/rk3568-dtu-demo.json"

if [ ! -x "$BIN_PATH" ]; then
  echo "binary not found: $BIN_PATH"
  echo "please copy rk3568-dtu-terminal-demo to ${APP_DIR}/bin/ first"
  exit 1
fi

exec "$BIN_PATH" "$CONFIG_PATH"
