#!/system/bin/sh
SKIPUNZIP=0
ui_print "========================================"
ui_print "  iUnlocker GLTool  v1.5.0"
ui_print "  Multi-game GPU spoof + AC hide"
ui_print "========================================"
ui_print "- Zygisk module (arm64 + arm32)"
ui_print "- Per-package config via WebUI"
ui_print "- Enable Zygisk, add games to DenyList"
ui_print "========================================"

case "$ARCH" in
  arm64|arm) ui_print "- ABI: $ARCH supported" ;;
  *) ui_print "! ABI $ARCH may not be supported" ;;
esac

mkdir -p /data/adb/iunlocker
chmod 755 /data/adb/iunlocker
ui_print "- config dir ready (no default game)"
# ensure later config.json is readable from app context
touch /data/adb/iunlocker/config.json 2>/dev/null
chmod 644 /data/adb/iunlocker/config.json 2>/dev/null
