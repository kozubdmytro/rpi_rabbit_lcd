# Smart Rabbit LCD gateway

Independent copy of the Smart Rabbit hub for a 3.5-inch 480x320 LCD
with XPT2046. The original 64x64 UI, fonts, menu, animations, Mesh,
Wi-Fi provisioning and local settings website are retained.

Each logical pixel becomes an exact 5x5 block (nearest neighbour).
The 320x320 image is centered at x=80, y=0. Both 80-pixel side areas
are black, with no labels, controls or other content. Rotation remains
inside the original 64x64 frame, as in the matrix version.

MPR121 and HUB75 code and dependencies are removed. Input is currently
released at all times: touchscreen control is explicitly deferred to the
next step. The existing gesture logic remains, but is not fed touch events.

## LCD prerequisites

The LCD's Linux display driver must already expose a **480x320 truecolor
framebuffer**, normally `/dev/fb1` on older SPI LCD setups. Use `--fb /dev/fb0`
if that is the LCD device. RGB565, 24-bit RGB and 32-bit RGB are supported,
using the driver's channel layout, row stride and visible offsets.
Unsupported modes fail explicitly instead of stretching the image.
XPT2046 identifies the touch controller, not the LCD controller; the exact
LCD overlay depends on the actual board. This repository does not install
overlays, change boot configuration or configure the screen automatically.
The framebuffer must not be used by another application or a console.
No desktop, browser, rpi-rgb-led-matrix or I2C touch library is required.

Brightness 0..100% scales RGB values in software; zero produces black.
It does not control or switch off the LCD backlight.

Framebuffer layout follows the [Linux framebuffer API](https://docs.kernel.org/fb/api.html).

## Build and run on Raspberry Pi

Dependencies: C++17 compiler, make, pkg-config, libglib2.0-dev,
BlueZ bluetoothd/bluetooth-meshd, D-Bus and NetworkManager.

```sh
make -j4
make test
sudo ./run.sh --fb /dev/fb1
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
