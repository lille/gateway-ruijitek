#!/bin/sh
set -eu

APP_ROOT=/opt/rk3568-dtu-demo
SERVICE_NAME=rk3568-dtu-demo.service
PKG_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

mkdir -p "$APP_ROOT/config" "$APP_ROOT/bin" "$APP_ROOT/scripts"
cp "$PKG_DIR/config/rk3568-dtu-demo.json" "$APP_ROOT/config/"
cp "$PKG_DIR/scripts/start.sh" "$APP_ROOT/scripts/"
chmod +x "$APP_ROOT/scripts/start.sh"

if [ -f "$PKG_DIR/bin/rk3568-dtu-terminal-demo" ]; then
  cp "$PKG_DIR/bin/rk3568-dtu-terminal-demo" "$APP_ROOT/bin/"
  chmod +x "$APP_ROOT/bin/rk3568-dtu-terminal-demo"
else
  echo "warning: binary not found in package bin/, copy it manually later"
fi

cp "$PKG_DIR/systemd/$SERVICE_NAME" "/etc/systemd/system/$SERVICE_NAME"
systemctl daemon-reload
systemctl enable "$SERVICE_NAME"
echo "installed. start with: sudo systemctl start $SERVICE_NAME"
