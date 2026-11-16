# Smart Rabbit LCD gateway

Independent copy of the Smart Rabbit hub for a 3.5-inch 480x320 LCD
with XPT2046. The original 64x64 UI, fonts, menu, animations, Mesh,
Wi-Fi provisioning and local settings website are retained.

Each logical pixel becomes an exact 5x5 block (nearest neighbour).
The 320x320 image is centered at x=80, y=0. The side areas now contain only the two touch strips
requested for MPR121-style control. Rotation remains
inside the original 64x64 frame, as in the matrix version.

MPR121 and HUB75 code and dependencies are removed. The XPT2046/ADS7846
resistive touchscreen now drives the existing gesture logic through two
vertical control areas at the physical edges. Both gray lines are 9 pixels
wide: x=6..14 and x=465..473, y=12..307. Touch targets are x=0..39 and
x=440..479 across the screen height.
Position increases downward. Taps, swipes and 700 ms holds retain their
original meaning. A touch must begin inside the strip; leaving it cancels
without a click. Touches on the 320x320 UI do nothing.

The input reader discovers ADS7846 by device name, so event numbering may
change. It reads nonblocking evdev packets and retries after input loss.
An optional local `touch-calibration.conf` uses the previous `SR_TOUCH_V1`
affine format; otherwise the default is uncalibrated landscape rotate=90.
The installed Pi reuses its existing calibrated file. Calibration is ignored
by Git. Software brightness also dims the strip; touch remains active at 0%.
The strips stay at the physical edges when UI rotation changes.
Each contact belongs to the side where it began; jumping sides cancels it.

## LCD prerequisites

The installed Pi uses the native ILI9486 DRM driver at **32 MHz SPI**:

```text
dtoverlay=piscreen,drm,rotate=180,speed=32000000
```

The LCD must expose 480x320. With `--fb /dev/fb0`, the application detects
`ili9486drmfb` and discovers the corresponding DRM card by driver name.
It sends only the changed bounding rectangle, including any changed strip
pixels. A full upload at startup and every two seconds clears stale pixels.
The native backend temporarily acquires the graphics console and the stock
CPU `performance` policy; both are restored on exit. The service's recovery
helpers cover abnormal termination. No overclock or voltage changes are used.

Measured on this Pi: **120 completed full-UI uploads in 6.77085 s = 17.723 FPS**,
with both touch strips present and zero SPI errors/timeouts. Smaller changes
need less data. This measures completed blocking DRM uploads, not the LCD's
optical scan rate. `build/lcd-performance.txt` on the Pi records the result.
The test verified every active RGB565 scanout pixel, exact 5x replication,
both strips, a partial update and brightness 0/50/100%.

The legacy FBTFT fallback remains available when its driver is selected.
It supports packed 16/24/32-bit truecolor with the driver's channel layout,
row stride and offsets, including fb_ili9486's RGB565 nonstd=1 flag.
Unsupported modes fail explicitly instead of stretching the image.
XPT2046 identifies the touch controller, not the LCD controller; the exact
LCD overlay depends on the actual board. The application itself does not
install overlays or change boot configuration. On the installed Pi, the
known working DRM overlay has been saved in `/boot/firmware/config.txt`.
No desktop, browser, rpi-rgb-led-matrix or I2C touch library is required.

Brightness 0..100% scales RGB values in software; zero produces black.
It does not control or switch off the LCD backlight.

Framebuffer layout follows the [Linux framebuffer API](https://docs.kernel.org/fb/api.html).

## Build and run on Raspberry Pi

Dependencies: C++17 compiler, make, pkg-config, libglib2.0-dev,
BlueZ bluetoothd/bluetooth-meshd, D-Bus and NetworkManager.
The native DRM backend also needs libdrm-dev (already installed on the Pi);
without it only FBTFT/headless output is built. Console recovery uses fuser.

```sh
make -j4
make test
sudo ./run.sh --fb /dev/fb0
```

Alternatively use CMake with the same dependencies:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake -j4
ctest --test-dir build-cmake --output-on-failure
```

Physical mode also starts Bluetooth Mesh and the website on port 8080.
Existing gateway and BlueZ Mesh state must be preserved together.
`--create-network` is only for deliberate first initialization of a new
network. This copy includes no keys, node records or Wi-Fi credentials.
Do not run two gateway instances against the same state or hardware.
The example service retains the original `/home/srlab/smartrabbit-test`
installation path; review it before installing on another device/path.

Preview without hardware, Bluetooth or state access:

```sh
./run.sh --headless --frames 90 --dump /tmp/rabbit-lcd
```

Produces a 64x64 logical PPM and a 480x320 preview with a centered 320x320
image. Preview brightness is applied as in physical output.
`--width`/`--height` affect preview only. Use `--help` for other options.

The website in `web/` manages nodes, menu, brightness and rotation.
`python3 web/simulate.py` starts its separate local simulator;
`python3 tests/web_simulation_test.py` checks that simulator.
The root `index.html` remains the standalone Pixel Studio editor.

BLE protocol/history documents are copied from the source project and
contain historical matrix/MPR121 references. They do not describe this
LCD backend; use this README and `handoff.md` for the current display state.
