# Raspberry Pi BLE gateway

## Wi-Fi-only phone GATT and local web control

This section supersedes the historical phone-control contract below and in
`BLE_MIGRATION.md`. Local HTTP on port 8080 now owns node and display management.
The old A004/A005/A008/A009/A00A/A00B exports and handlers are commented out,
not deleted. They are absent from active GATT discovery.

The advertised name remains `SRLAB_THPS_MAIN`, primary service `00A0`. Active
UUIDs use the standard Bluetooth base UUID, e.g.
`0000a00c-0000-1000-8000-00805f9b34fb`:

| UUID | Access | Payload |
| --- | --- | --- |
| A00C | Encrypted WRITE with response | SSID fragment: byte offset u8, then UTF-8 bytes; max assembled length 32 bytes |
| A00D | Encrypted WRITE with response | Password fragment: byte offset u8, then UTF-8 bytes; 0 or 8..63 bytes, or 64 hexadecimal digits |
| A00E | Encrypted WRITE with response; READ, NOTIFY | Write one byte `01` to apply; read/notify UTF-8 `idle`, `connecting`, `connected` or `error` |

Fragment offset zero replaces a field; every later offset must equal the
assembled byte length. At MTU 23, send at most 19 credential bytes per write,
plus the offset byte. ATT offsets must be zero; prepared/reliable writes and
Write Without Response are rejected. Both fields must be written before apply.
An open network uses a password write containing only offset byte `00`.
Credentials are scoped to the phone connection and cleared on disconnect/apply.
SSID/password values are write-only and are never present in status or HTTP.

Example SSID `Home`: A00C = `00 48 6f 6d 65`; password `12345678`:
A00D = `00 31 32 33 34 35 36 37 38`; then A00E = `01`.
These are protocol examples, not real credentials. BLE link encryption is
required; the phone may need to pair using the operating system's normal flow.
The phone client must implement this protocol; its source is not in this repo.

NetworkManager must be running with managed interface `wlan0`, and the gateway
must have its existing root/system-bus access. Only the application's fixed-UUID
`smartrabbit-wifi` profile is created/updated through NetworkManager D-Bus;
other profiles and Netplan files are retained. Supported networks are open or
WPA-PSK personal networks; enterprise credentials are outside this protocol.
See the [NetworkManager activation API](https://www.networkmanager.dev/docs/api/latest/gdbus-org.freedesktop.NetworkManager.html).

Phone GATT and advertising are disconnected/unregistered after NetworkManager
reports device activation and a usable IPv4 address. Internet availability is
not required. On Wi-Fi loss the app automatically advertises again; the phone
client must reconnect (a BLE peripheral cannot force a central connection).
The Bluetooth controller is never powered down because Mesh needs it.

Deletion from the web performs remote Mesh Node Reset first, waits for status,
then executes the same local key/journal cleanup used by FORGET. Reset timeout
retains the node. If local deletion failed after confirmed reset, retry uses
FORGET with the retained reset evidence. No raw local-only FORGET is exposed
in the web page. Existing node firmware handles the standard Mesh reset; no
node firmware was modified or flashed as part of this change.

Verification on 2026-11-13: the complete application was built, linked and
installed on SRLAB. Both `make test` executables and the headless preview passed.
The real HTTP server passed asset, brightness 49 -> 50, invalid-value and
cross-origin-write rejection checks. Gateway state remained byte-identical after
restoring brightness; Wi-Fi was connected and phone ActiveInstances was zero.
The user confirmed the physical panel displayed LOADING after startup recovery.
Wi-Fi-loss recovery, BLE credential writes/encryption and actual node key erasure
still require hardware acceptance checks. No nodes were paired or reset.

A prior interrupted BIND left a pending UUID without any reserved address/count,
which correctly prevented normal startup. Recovery verified the matching daemon
token, zero node records and only the hub's own device key, then backed up the
journal and cleared only that pending UUID. Keys, allocation, rotation and
brightness were preserved. See `BLE_BLOCKER.md` for the recovery checkpoint.

## Historical deployment notes

An **unverified** BCM43455 scan/TX workaround is provided in
`deploy/bluez-5.82-mesh-scan-tx.patch`; see `BLE_BLOCKER.md` for the current
pairing issue, exact scope and rollback. `deploy/build-mesh-workaround.sh`
compiles only the Mesh daemon using the Raspberry Pi OS 5.82-1.1+rpt2 sources
and distribution patches. Build prerequisites: `libell-dev`, `libdbus-1-dev`,
`libreadline-dev`, `libjson-c-dev`, the existing GLib development package,
compiler, make, patch, curl and pkg-config. It does not install files or run
tests. The separate daemon and service drop-in leave the packaged Bluetooth
daemon and Mesh storage in place; phone GATT uses the existing application.

Deployment status (2026-11-12): the LCD full-brightness application was previously
compiled and installed. The separate Mesh daemon workaround above has now been
compiled on SRLAB and installed over SSH; Mesh and app service restart commands
completed. No tests or additional runtime verification were performed. Wi-Fi
profiles, Netplan, boot recovery files and Mesh keys were not changed. Pairing
verification is left to the user; do not initiate pairing or reset nodes.

The deployed startup fixes install the local AppKey before vendor-model binding
and use BlueZ's remote-device-key addressing for local loopback. The HUB75 output applies A008 directly as 0..50 percent brightness; zero remains off.

## Platform requirements

The application is C++17 and uses GLib's GIO D-Bus client (`gio-2.0`, development
package `libglib2.0-dev`) plus the rpi-rgb-led-matrix library. Runtime needs
BlueZ `bluetoothd` and `bluetooth-meshd` exposing their system-bus APIs. The
distribution may package the Mesh daemon separately (commonly `bluez-meshd`).
SRLAB uses Raspberry Pi OS BlueZ 5.82-1.1+rpt2 and its MGMT Mesh backend on the
built-in BCM43455. The required daemon build libraries were installed without
upgrading or removing existing packages. Radio reliability remains unverified.

The Mesh daemon owns encryption, device/App/Net keys, IV state, segmentation
and sequence persistence. Its persistent storage must be retained alongside
the application's state directory. Never delete either as a recovery shortcut.
The application does not implement cryptography. The separate, unverified Mesh
daemon patch above changes radio scheduling only.

Use the daemon's MGMT-capable Mesh I/O path for shared-controller operation.
The legacy raw-HCI Mesh path can take exclusive ownership of an adapter and
prevent phone GATT operation on that adapter. If the installed kernel/BlueZ/
controller cannot share it, a separate Mesh adapter is required: configure the
Mesh daemon for that adapter and use `--phone-adapter` for bluetoothd's adapter.
The custom Mesh service drop-in selects the separate patched daemon. Adapter,
driver and controller firmware settings are unchanged.
Single-controller coexistence requires hardware verification; it is not claimed
to work merely because this source is present.

## Build and first initialization — deferred

After authorization, install the missing distribution dependencies and build
the executable with `make build/smartrabbit-test`. This target builds the app
without running tests; `build.sh` still invokes the old core tests automatically.
CMake also declares GIO as a required dependency. Existing UI tests are retained
but do not establish Mesh/GATT behavior; old device scripts with removed demo
CLI options must not be used as gateway acceptance checks.

Before deploying a new binary, stop the existing display service to avoid
competing for the LCD. With both BlueZ daemons configured/running, initial
network creation will be explicit:

```sh
cd /home/srlab/smartrabbit-test
sudo ./build/smartrabbit-test --create-network
```

This creates a **new** network but does not automatically pair a node.
`--create-network` refuses an existing `mesh-state/gateway.conf`. Subsequent
starts omit that flag. Normal startup refuses missing, corrupt or incomplete
state; it never recreates a network after an attach error. Alternate storage
can be supplied with `--state-dir PATH`. The daemon's state and the app's
state must describe the same network. Treat the app token as sensitive.

The existing systemd service still launches the same executable from its
working directory. Before enabling the migrated service, its startup ordering
must be coordinated with both BlueZ daemons and initial state must exist.
No unit was installed, enabled or restarted by this change.

## Phone and node operation

The phone sees `SRLAB_THPS_MAIN`, service `00A0`, and A004/A005/A008/A009/A00A/
A00B with the existing binary payloads. Discovery is **PB-ADV** now; nodes must
advertise an unprovisioned Mesh beacon and support this bearer. Node firmware
has not been changed. Existing S3-paired nodes will need resetting/re-pairing
later; no keys were migrated. The current node firmware's PB-ADV capability
has not been verified from its separate source or on hardware.

Use the existing phone commands: SCAN_START, CAPTURE/read A00A, BIND, and
RETRY_CONFIG if needed. The app serializes AppKey Add, vendor model binding
and publication setup to gateway address 0001. RESET requires a remote reset
status before local deletion; FORGET is the explicit local-only alternative.
Never interpret an accepted command write as a completed operation: read or
subscribe to A00B. A005 contains only the latest BME record and latest moisture
record, not the whole inventory, as before.

The screen shows only rounded BME280 temperature and humidity from the latest
accepted TempHumi report. It shows loading until one arrives, when that node
is deleted, on a gateway fault, or at 14 seconds without a fresh report.
Duplicate transactions do not refresh this timer. Moisture and chip temperature
are decoded but never replace the display values. No clock, demo menu, HTTP
server, HTML assets, local BME or accelerometer feature is introduced.

A004 preserves rotation 1-4 (identity, clockwise 90, 180, clockwise 270), default
3. A008 preserves 0-50, with a new-install default of 50. For the temporary LCD,
every nonzero value uses 100 percent RGB output, including an existing saved low
value. READ still returns the stored BLE value; intermediate levels do not dim
this LCD. A future matrix backend needs its own brightness mapping.
Zero clears the screen once while the gateway continues processing. The physical
backlight is unchanged. Settings commit before the GATT success response and are
restored on restart. The old demo brightness file is no longer used. Touch has
no actions on the read-only readings screen; manual calibration is retained.

## Explicit Linux differences

- PB-ADV replaces PB-GATT for provisioning, as approved by the user.
- Invalid scalar values use BlueZ's unlikely-error (0x0E) mapping instead of
  ESP32's 0xFF, as approved. Application errors 0x80-0x84 remain distinct.
  BlueZ owns CCC validation, MTU negotiation and advertisement packet layout.
- The Pi keeps Mesh reception active. BlueZ's application API does not expose
  the S3's selective receive pause/resume. REPORT/ACK/READY transactions and node
  sleep eligibility are retained; this port does not claim the S3's radio power
  savings. Discovery callbacks are subject to BlueZ's duplicate filtering.
- The Pi uses its own advertising identity. The app does not force the S3's
  derived static-random address or alter adapter identity settings.
- Daemon/system-bus failures stop Bluetooth processing and display loading.
  Restart reattaches to the saved token; it does not reset the network.

## Recovery and deferred acceptance

The app locks its state directory and saves its journal using atomic replacement
and file/directory fsync. Address allocations are reserved before provisioning
continues and never automatically reused. If a crash leaves provisioning in
progress, startup refuses the incomplete journal. Reconcile it with the daemon
and the physical node before proceeding; do not erase keys or just rerun network
creation. Removal intent/evidence is also retained until cleanup commits; an
explicit FORGET can retry a retained deletion. Readings are RAM-only and must be
fresh after every restart.

Before calling the migration verified, authorize and perform a C++ build, host
checks for codecs/state transitions, exact GATT payload/error checks, and actual
Pi testing for PB-ADV, both node types, reset/forget failures, concurrent phone/
Mesh traffic, long inventory reads, reconnect subscriptions, freshness/loading,
zero brightness/rotation, daemon restarts and persistent state. These have not
been run. No runtime, RF, power or persistence success is asserted.
