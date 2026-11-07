#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH_DIR="$DIR/campus_safety_node"
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"

# Setup ARM64 python wrapper if needed
mkdir -p /tmp/arm64_bin
cat << 'PYEOF' > /tmp/arm64_bin/python3
#!/bin/bash
exec /usr/bin/arch -arm64 /usr/bin/python3 "$@"
PYEOF
chmod +x /tmp/arm64_bin/python3
export PATH="/tmp/arm64_bin:$PATH"

echo "=== 1. Checking Serial Port ==="
PORT="$1"
if [ -z "$PORT" ]; then
  # Auto-detect
  PORT=$(ls /dev/cu.usb* /dev/cu.wch* /dev/cu.SLAB* /dev/cu.CP210* 2>/dev/null | head -n 1 || true)
fi

if [ -z "$PORT" ]; then
  echo "❌ ไม่พบพอร์ต ESP32 (เช่น /dev/cu.usbserial*, /dev/cu.wchusbserial*)"
  echo "กรุณาตรวจสอบว่า:"
  echo "  1. เสียบสาย USB ระหว่าง ESP32 กับ Mac หรือยัง"
  echo "  2. สาย USB เป็นสาย Data (รับส่งข้อมูล) หรือไม่ (ระวังสาย Charge-only)"
  echo "  3. หากใช้ Adapter USB-C ลองเสียบใหม่หรืออนุญาต Allow accessory to connect"
  exit 1
fi

echo "✅ ตรวจพบพอร์ต: $PORT"

echo "=== 2. Compiling Sketch ==="
"$ARDUINO_CLI" compile --fqbn esp32:esp32:esp32 "$SKETCH_DIR"

echo "=== 3. Uploading Firmware to ESP32 ($PORT) ==="
"$ARDUINO_CLI" upload -p "$PORT" --fqbn esp32:esp32:esp32 "$SKETCH_DIR"

echo "=== 4. Upload Complete! Starting Serial Monitor (115200 baud) ==="
echo "💡 กด Ctrl + C เพื่อออกจาก Serial Monitor"
"$ARDUINO_CLI" monitor -p "$PORT" -c baudrate=115200
