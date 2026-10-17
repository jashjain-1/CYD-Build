import serial
import subprocess
import time
import re
import sys

ADB = ["adb", "-s", "acea07d9"]

def run_adb(cmd):
    return subprocess.run(ADB + cmd, capture_output=True, text=True)

def main():
    print("Opening serial port COM4 without reset...")
    ser = serial.Serial()
    ser.port = 'COM4'
    ser.baudrate = 115200
    ser.dtr = False
    ser.rts = False
    ser.timeout = 0.2
    ser.open()
    ser.read(10000)

    print("Intentionally resetting HUD into fresh boot...")
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False

    print("Waiting for HUD to complete boot...")
    booted = False
    start_boot = time.time()
    while time.time() - start_boot < 10:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            safe = line.encode('ascii', errors='replace').decode('ascii')
            print(f"[HUD BOOT] {safe}")
            if "BOOT boot_to_ui_ms=" in line or "PAIR no bonded phone" in line:
                booted = True
                break

    if not booted:
        print("FAIL: HUD did not boot within 10s")
        ser.close()
        return 1

    time.sleep(0.5)
    print("HUD is advertising. Triggering Pair on phone...")
    run_adb(["shell", "input", "tap", "214", "403"])

    passkey = None
    start = time.time()
    print("Listening for passkey from HUD...")
    while time.time() - start < 15:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            safe = line.encode('ascii', errors='replace').decode('ascii')
            print(f"[HUD PAIR] {safe}")
            m = re.search(r"passkey=(\d+)", line)
            if m:
                passkey = m.group(1)
                print(f"--> DETECTED PASSKEY: {passkey}")
                break

    if not passkey:
        print("FAIL: No passkey detected within 15 seconds")
        ser.close()
        return 1

    print(f"Entering passkey {passkey} into phone...")
    time.sleep(0.8)
    run_adb(["shell", "input", "text", passkey])
    time.sleep(0.5)
    print("Tapping Save on pairing dialog (786, 1320)...")
    run_adb(["shell", "input", "tap", "786", "1320"])

    print("Waiting for AUTH COMPLETE...")
    auth_success = False
    start_auth = time.time()
    while time.time() - start_auth < 15:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            safe = line.encode('ascii', errors='replace').decode('ascii')
            print(f"[HUD AUTH] {safe}")
            if "BLE: AUTH COMPLETE success=1" in line:
                auth_success = True
                print("--> PAIRING AND BONDING CONFIRMED SUCCESSFUL!")
                break
            elif "BLE: AUTH FAILED" in line:
                print("--> AUTH FAILED!")
                break

    ser.close()
    
    # Capture post-pairing screenshot on phone
    time.sleep(1.0)
    run_adb(["shell", "screencap", "-p", "/sdcard/paired_screen.png"])
    run_adb(["pull", "/sdcard/paired_screen.png", "docs/evidence/20261016/TC-E2E-01/paired_screen.png"])

    return 0 if auth_success else 2

if __name__ == "__main__":
    sys.exit(main())
