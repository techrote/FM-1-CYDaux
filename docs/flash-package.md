# Install FM-1 CYDaux on the CYD

This package is for the original **ESP32-2432S028R**, with a 320×240 ILI9341
display and XPT2046 resistive touch. It is an early, compiled and software-tested
build. Physical board/FM-1 acceptance remains pending. It runs on the **CYD**;
the M-VAVE FM-1 keeps its own separately installed firmware.

## Flash from Windows or Linux

Extract the whole package. Use Python 3.11 and a USB data cable to the CYD.
Open a terminal in this directory. On Windows, `py` may be used for `python`.

```console
python -m pip install -r requirements-flash.txt
python flash.py --check
python flash.py --port COM5
```

Replace `COM5` with the CYD's actual serial port. On Linux this commonly looks
like `/dev/ttyUSB0`; permission to open that port is needed. The script checks
the manifest’s board label, binary sizes, offsets and SHA-256 hashes before opening a port,
then tells esptool to require an ESP32 chip. Esptool cannot identify the PCB’s
screen model; confirm the printed ESP32-2432S028R board marking yourself. It never auto-selects a serial device.
Use `--baud 115200` if 460800 is unreliable on the USB cable/adapter.

The three images are bootloader at `0x1000`, partition table at `0x8000`, and
application at `0x10000`. The script does not erase the whole flash or overwrite
the NVS range used for CYDaux settings. Installing it replaces the CYD's previous
application. There is no OTA/update server in this version.

If automatic bootloader entry fails, hold **BOOT**, press/release **RESET**, then
release BOOT when esptool connects. Press RESET after writing if needed.

## First boot

Press and hold the three displayed crosses, releasing between each. A fourth
independent corner checks the calibration before saving it. A failed check
restarts calibration. The device then opens in **DEMO** mode, which is usable
without the FM-1 or computer. Hold BOOT for about two seconds **while the app is
already running** to repeat calibration; do not hold BOOT during power-on unless
you intend to enter the chip's serial bootloader.

Open **LINK → SETUP** for connection settings. Join the Wi-Fi name and password
shown on the CYD, then open **http://192.168.4.1/**. Enter the home Wi-Fi name,
computer's address, bridge port and token. For direct Melodee BLE, an exact BLE
address is optional. Save; the temporary setup network closes. Choose BLE or
Wi-Fi and press CONNECT. A connection is never started automatically at boot.

The full guide, supported firmware revisions, simulator and USB bridge live at
<https://github.com/techrote/FM-1-CYDaux>. `manifest.json` identifies the source
commit and hashes. `firmware.elf`/`.map` support diagnosis and memory inspection.
The repository's pinned PlatformIO configuration and dependency source links
provide the build inputs needed to rebuild or modify the application.
