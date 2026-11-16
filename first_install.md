# First installation: Smart Rabbit LCD

Target: Raspberry Pi 4B, Raspberry Pi OS Lite, and the same 3.5-inch
480x320 SPI ILI9486 display with XPT2046/ADS7846 resistive touch as the
original installation. Commands below run on the **new Pi**, over SSH.
Use your own Linux username; no `srlab` account is required.

This project includes Bluetooth Mesh for nodes, BLE GATT for phone Wi-Fi
provisioning, and a local web interface for node/display management.
The display shows the original 64x64 UI enlarged to 320x320 with side touch
strips. The repository contains no network keys, node records, Wi-Fi passwords,
or device-specific touch calibration.

## 1. Hardware and OS

- Raspberry Pi 4B, suitable power supply, microSD card, and network access.
- Matching ILI9486/XPT2046 SPI LCD HAT, installed with the Pi powered off,
  following the board manufacturer's pinout. XPT2046 identifies the touch
  controller only; it does not prove that the LCD controller/pinout matches.
- Built-in Bluetooth and Wi-Fi enabled. Ethernet can be used for initial setup.
- Compatible Bluetooth Mesh nodes supporting PB-ADV provisioning. Nodes already
  provisioned to another network cannot simply join a new one.

Flash Raspberry Pi OS Lite using Raspberry Pi Imager. A current 64-bit image
is preferred. Configure a username, SSH, Wi-Fi credentials/country if needed,
and hostname in Imager. Boot and connect:

```sh
ssh YOUR_USER@YOUR_PI_HOST_OR_IP
cat /etc/os-release
uname -a
```

The original installation's DRM driver needs a kernel/overlay supporting
`piscreen,drm`. An older image may lack this support. Check the installed
overlay help before proceeding; do not substitute random vendor install scripts.

## 2. Install dependencies and download the project

```sh
sudo apt update
sudo apt install -y git ca-certificates build-essential pkg-config \
  libglib2.0-dev libdrm-dev bluez bluez-meshd dbus network-manager \
  python3 psmisc curl evtest
git clone https://github.com/kozubdmytro/rpi_rabbit_lcd.git "$HOME/rpi_rabbit_lcd"
cd "$HOME/rpi_rabbit_lcd"
```

Package roles:

| Component | Packages / provider |
| --- | --- |
| C++17 compiler, Make and headers | `build-essential` |
| Library discovery | `pkg-config` |
| GIO system D-Bus client | `libglib2.0-dev` |
| Native DRM display access | `libdrm-dev` and its APT dependencies |
| Phone BLE GATT, Bluetooth tools | `bluez` (`bluetoothd`) |
| Mesh daemon | `bluez-meshd` (`bluetooth-meshd`) |
| System bus | `dbus` |
| Wi-Fi provisioning backend | `network-manager` |
| Service console recovery | `python3`, `psmisc` (`fuser`) |
| Diagnostics | `curl`, `evtest` |
| SPI, ILI9486 DRM and ADS7846 input drivers | Raspberry Pi OS kernel and `piscreen` overlay |

APT installs the required runtime libraries transitively. CMake is optional
(`sudo apt install cmake`); the instructions use Make. No Python pip packages,
desktop, X server, browser, HUB75 library, MPR121 library, or local sensor driver
is needed. Sensors reside on compatible remote nodes.

If `bluez-meshd` is unavailable, check the OS release and package sources;
the gateway cannot operate with `bluetoothd` alone. Do not continue with Mesh
silently omitted. Use the distribution's Pi kernel/firmware packages if the
overlay or driver is missing, reboot, and repeat the checks below.

## 3. Enable the LCD and touchscreen drivers

```sh
dtoverlay -h piscreen
```

Confirm that `drm` is a supported parameter. On current Raspberry Pi OS the
boot configuration is `/boot/firmware/config.txt`; older installations may use
`/boot/config.txt`. Back up and edit the actual file. For the current path:

```sh
sudo cp -a /boot/firmware/config.txt /boot/firmware/config.txt.before-rabbit
sudo nano /boot/firmware/config.txt
```

Under an applicable `[all]` section, configure these lines once:

```ini
dtparam=spi=on
dtoverlay=piscreen,drm,rotate=180,speed=32000000
```

Review any existing LCD overlays so two drivers do not claim the same SPI
devices. Retain unrelated settings. Do not disable Bluetooth. This profile
uses the stock CPU settings; 50 MHz SPI previously produced display artifacts
on the original board, so use 32 MHz.

```sh
sudo reboot
```

Reconnect over SSH, then check:

```sh
cat /proc/fb
ls -l /dev/fb* /dev/dri/card*
grep -A 8 -B 2 -i ads7846 /proc/bus/input/devices
sudo dmesg | grep -Ei 'ili9486|ads7846|spi|drm'
```

Find the framebuffer named `ili9486drmfb`. In the original setup it is
`/dev/fb0`, but enumeration can differ. Check its geometry, substituting the
actual framebuffer name:

```sh
cat /sys/class/graphics/fb0/name
cat /sys/class/graphics/fb0/virtual_size
```

The screen must expose 480x320. The examples below assume `/dev/fb0`; replace
that argument everywhere if yours differs. The application discovers the DRM
card by driver name; do not hardcode a DRM card number. The overlay also
provides ADS7846 touch input; there is no separate userspace touch driver.

## 4. Start and check Bluetooth and Wi-Fi components

```sh
sudo systemctl enable --now bluetooth.service bluetooth-mesh.service NetworkManager.service
sudo rfkill unblock bluetooth
sudo bluetoothctl power on
systemctl status bluetooth.service bluetooth-mesh.service NetworkManager.service --no-pager
busctl --system introspect org.bluez.mesh /org/bluez/mesh org.bluez.mesh.Network1
nmcli device status
systemctl cat bluetooth-mesh.service
```

Mesh must expose `org.bluez.mesh.Network1`, including `Attach`. For phone
Wi-Fi setup, NetworkManager must manage `wlan0`. Keep your existing working
SSH/network connection; do not replace network configuration blindly over SSH.
Use `sudo nmtui` locally or through a reliable connection if Wi-Fi setup is
needed. The phone protocol supports open and WPA-PSK personal networks.

Mesh and phone GATT share the built-in controller only when the installed
kernel/BlueZ/controller support the MGMT Mesh path. Raw-HCI exclusive ownership
can prevent phone GATT from working. Inspect the daemon's installed help and
logs before changing its I/O configuration; do not assume coexistence works
just because both services start. If sharing fails, a compatible separate Mesh
Bluetooth adapter may be needed; `--phone-adapter hci0` selects the phone side,
while Mesh adapter selection belongs to the daemon configuration.

## 5. Build and check without hardware access

```sh
cd "$HOME/rpi_rabbit_lcd"
pkg-config --modversion gio-2.0 libdrm
make -j4
make test
./build/smartrabbit-test --headless --frames 90 --dump /tmp/rabbit-lcd
```

Expect successful tests and `/tmp/rabbit-lcd-logical.ppm` plus
`/tmp/rabbit-lcd-preview.ppm`. The preview does not access Bluetooth or saved
state. Install `libdrm-dev` **before** building; DRM is optional at build time,
and an existing build made without it must be rebuilt (`make -B -j4`) after
installing the package. These tests do not establish RF or touch operation.

## 6. Initialize a new Mesh network and run

Choose before proceeding:

- **Independent new Pi/network:** use the first-initialization command below.
- **Replacement for the old gateway with existing nodes:** do not create a new
  network. Stop both gateways and transfer a consistent backup of the original
  application's full state directory together with its corresponding BlueZ
  Mesh storage (normally `/var/lib/bluetooth/mesh`, verify the daemon settings),
  preserving ownership and permissions. This is a migration, not the fresh
  installation described here. Never run cloned gateways concurrently.

For a genuinely new network, with no other gateway process running:

```sh
cd "$HOME/rpi_rabbit_lcd"
sudo ./build/smartrabbit-test --fb /dev/fb0 \
  --state-dir "$HOME/rpi_rabbit_lcd/mesh-state" --create-network
```

Keep it running while checking the display and web interface from another
terminal. Creation does not automatically pair nodes. Press Ctrl+C to stop
cleanly. Every subsequent start omits `--create-network`:

```sh
sudo ./build/smartrabbit-test --fb /dev/fb0 \
  --state-dir "$HOME/rpi_rabbit_lcd/mesh-state"
```

Normal startup refuses missing/corrupt/incomplete state. Do not delete the
state or rerun creation to bypass errors. Preserve the application state and
BlueZ storage together, privately; they contain credentials and persistent
Mesh counters/keys.

## 7. Install autostart for this Pi's actual path

Stop the manual application with Ctrl+C first. The repository's existing
deployment scripts/unit contain paths for the original Pi. Do not run them
unchanged on this new installation. Instead create this path-adjusted unit
from the normal login user's shell, in the cloned project:

```sh
cd "$HOME/rpi_rabbit_lcd"
APP_DIR="$(pwd -P)"
sudo tee /etc/systemd/system/smartrabbit-test.service >/dev/null <<EOF
[Unit]
Description=Smart Rabbit LCD Mesh Gateway
After=local-fs.target bluetooth.service bluetooth-mesh.service NetworkManager.service
Requires=bluetooth.service bluetooth-mesh.service
Wants=NetworkManager.service
StartLimitIntervalSec=0

[Service]
Type=simple
User=root
WorkingDirectory=${APP_DIR}
ExecStartPre=/usr/bin/python3 "${APP_DIR}/deploy/recover-console.py"
ExecStart=${APP_DIR}/build/smartrabbit-test --fb /dev/fb0 --state-dir "${APP_DIR}/mesh-state" --web-dir "${APP_DIR}/web"
ExecStopPost=/bin/sh "${APP_DIR}/restore-performance.sh"
ExecStopPost=/usr/bin/python3 "${APP_DIR}/deploy/recover-console.py"
RuntimeDirectory=smartrabbit-test
RuntimeDirectoryMode=0755
KillSignal=SIGINT
TimeoutStartSec=20
TimeoutStopSec=5
Restart=always
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
sudo systemd-analyze verify /etc/systemd/system/smartrabbit-test.service
sudo systemctl daemon-reload
sudo systemctl enable --now smartrabbit-test.service
sudo systemctl status smartrabbit-test.service --no-pager
```

Use the ordinary home path shown above, without spaces or systemd `%`
specifiers. If installing elsewhere, quote/escape executable paths correctly
and verify the generated unit before enabling it. Do not put
`--create-network` in the service. On an already configured Pi, inspect existing
service drop-ins (`systemctl cat smartrabbit-test`) before changing the unit;
old overrides may supersede its paths.

## 8. Verify the complete installation

```sh
curl --fail http://127.0.0.1:8080/api/state
hostname -I
sudo journalctl -u smartrabbit-test -u bluetooth-mesh -b -n 100 --no-pager
systemctl show smartrabbit-test -p ActiveState -p NRestarts
```

Open `http://PI_IP:8080` from a device on the same trusted LAN. It should load
the local UI; API state should report a ready gateway. Keep this HTTP control
interface on a trusted network; do not expose port 8080 to the internet.

Check these physical behaviors:

- LCD: centered 320x320 image, sharp 5x5 logical pixels, two side strips.
- Touch: both side margins accept taps, swipes and holds; a captured drag
  continues across the screen until release. Touches beginning in the central
  UI are intentionally ignored.
- Display settings: rotation and brightness survive a restart. Brightness zero
  produces black pixels but does not switch off the physical LCD backlight.
- Mesh: use the web UI to discover/provision a compatible unprovisioned test
  node, check received readings, then verify recovery after gateway restart.
  Real climate data requires an assigned compatible BME node.
- BLE phone Wi-Fi provisioning: when Wi-Fi is unavailable, the phone should
  discover `SRLAB_THPS_MAIN`, service `00A0`. The phone client must implement
  encrypted A00C/A00D credential-fragment writes and A00E apply/status as defined
  in the current top section of `BLE_SETUP.md`. An arbitrary phone client does
  not implement this automatically. GATT advertising stops after usable Wi-Fi
  connectivity and resumes on loss; absence while connected is expected.

After successful manual checks, reboot and repeat the service, API, display
and touch checks to verify boot-time driver loading and autostart.

## 9. Touch alignment and troubleshooting

The application automatically discovers ADS7846 by name, not event number.
Use `sudo evtest` and select the ADS7846 device to inspect raw contacts; stop
the application first when testing touch alignment. Exit evtest with Ctrl+C.

With no `touch-calibration.conf`, the app uses its default landscape transform.
A different physical panel may need calibration. There is no interactive
calibration utility in this LCD copy. The optional file lives in the project's
working directory, is ignored by Git, and contains:

```text
SR_TOUCH_V1
a b c d e f
```

Replace the letters with six finite affine coefficients. For normalized raw
coordinates `nx,ny` (using the input device's ABS min/max), the mapping is
`pixel_x=(a*nx+b*ny+c)*479`, `pixel_y=(d*nx+e*ny+f)*319`.
Derive coefficients from at least three non-collinear measured contacts at
known screen positions, then check both edges. Default coefficients are
`0 -1 1 1 0 0`. Copy an old calibration only if it matches this panel and
orientation. Restart after changing the file.

| Symptom | Check |
| --- | --- |
| No framebuffer / wrong dimensions | Correct board, overlay, kernel, boot config section and completed reboot |
| DRM unavailable | `libdrm-dev` present before build; `ili9486drmfb` selected; rebuild if necessary |
| Display busy / console recovery refused | Another process owns the LCD or console; stop that process cleanly |
| No touch | ADS7846 kernel device in `/proc/bus/input/devices`; overlay/pinout; raw evtest events |
| Gateway attach error | Mesh service/API, matching daemon and app state; inspect logs, preserve keys |
| Wi-Fi provisioning fails | NetworkManager-managed `wlan0`, phone pairing/encryption and current GATT protocol |
| No phone advertisement | Wi-Fi already connected, Bluetooth adapter ownership, daemon logs |
| Web page missing | Port 8080, service status, `web/` assets and unit working directory |
| Repeated failures only during boot | Bluetooth readiness/order; inspect journal before adapting the original path-specific startup helper |
| Node discovery/provisioning stalls | PB-ADV support, node's existing provisioning state, RF and BlueZ/controller coexistence |

### Optional historical BlueZ workaround

The original Pi has a separately built BlueZ 5.82 Mesh daemon with the
BCM43455 scan/TX workaround in `deploy/bluez-5.82-mesh-scan-tx.patch`.
Its RF reliability is not established by the LCD tests. It is **not** a
mandatory display dependency or a verified fix for all Pi OS versions.

If reproducing that specific environment for an investigated Mesh issue,
read `BLE_BLOCKER.md`, `BLE_SETUP.md`, and the build script first. Extra build
dependencies are:

```sh
sudo apt install -y libell-dev libdbus-1-dev libreadline-dev libjson-c-dev patch xz-utils
sh deploy/build-mesh-workaround.sh
```

The script downloads pinned BlueZ 5.82 source, checks checksums, applies
distribution/local patches, and builds only the Mesh daemon. It does not
install or activate it. Its output is
`.local/bluez-mesh-scan-tx/bluez-5.82/mesh/bluetooth-meshd`.
Do not activate it blindly against a newer distribution. If a targeted test
requires it, preserve the packaged daemon and storage, install the separate
binary, and adapt the example `deploy/bluetooth-mesh-scan-tx.conf` drop-in
after checking the installed service's arguments. Keep a rollback to the
packaged daemon and test actual provisioning/traffic.

## Sources and limits

Project-specific commands and dependencies were checked against `Makefile`,
`src/main.cpp`, `src/lcd_touch.h`, `src/web.cpp`, `deploy/`, and `handoff.md`.
Older BLE documents include superseded HUB75/MPR121 and phone-management
details; their historical sections do not describe the current LCD UI.

- Repository: https://github.com/kozubdmytro/rpi_rabbit_lcd
- Official overlay reference: https://github.com/raspberrypi/firmware/blob/master/boot/overlays/README

This guide was prepared from the source and the original Pi's recorded setup.
It has not been executed end-to-end on a freshly imaged second Pi. Successful
build/unit tests alone do not verify Bluetooth RF, phone interaction, physical
touch calibration, or startup after reboot.
