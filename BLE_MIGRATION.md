# BLE gateway migration: compatibility contract

Status: **PB-ADV implementation prepared, unbuilt and untested** (2026-11-10).
The user approved PB-ADV instead of PB-GATT and BlueZ's invalid-value error
mapping. The protocol/state core is implemented in `src/mesh/gateway.*`.
Bluetooth/display integration is included in the Pi build targets. See
`BLE_SETUP.md` for current setup, accepted changes and remaining verification.
No build, test, hardware inspection, deployment or pairing has been run.

`src/mesh/` contains C++17 protocol/state helpers, a gateway worker and the BlueZ
adapter. The app now consumes the gateway's live reading snapshot. The rest of
this document records the ESP32 behavior contract; Linux differences listed in
`BLE_SETUP.md` take precedence where that platform cannot reproduce S3 controls.

## Source and boundaries

Reference: `D:\desktop\blemesh\upd6\esp32_rabbit_project\main`.
The reference checkout is on `master` at
`25d58b5dbeb818a38488f920893d47a7831ba899`, **with existing uncommitted changes**.
The current on-disk source, not just that commit, defines this contract.
`migration/esp32-source-sha256.json` identifies the relevant source file bytes.
Do not edit, format, build, clean, flash, commit or reset the reference project.
Do not copy Wi-Fi credentials or ESP32 NVS/Mesh secrets into this repository.

Target: this C++17 Pi project. It will replace the S3, create a fresh Mesh
network, and retain its own state across restarts. Existing nodes will be reset
and paired again later with explicit permission. No existing keys are imported.
There is no HTTP/HTML dashboard or accelerometer port in the Pi migration.
The ESP32's existing dashboard files must remain untouched.

## Backend compatibility gate

The reference explicitly enables `ESP_BLE_MESH_PROV_GATT`, rejects discovery
events without that bearer, and uses that bearer for BIND. It is not sufficient
to replace this with another provisioning bearer, even if a node supports it.

Upstream BlueZ's local Mesh provisioning initiator calls `pb_adv_reg` in
`initiator_open_cb`; its Mesh `Management1.AddNode` API provides no local
PB-GATT bearer selector. Its provisioner tool is documented as PB-ADV based.
Consequently the inspected stock Mesh daemon is not a drop-in replacement for
the required PB-GATT provisioning path. This conclusion comes from source/API
inspection, not a hardware experiment. The Pi's installed BlueZ version and
controller capabilities have not been inspected remotely.

References inspected on 2026-11-10:

- [BlueZ provisioner implementation](https://github.com/bluez/bluez/blob/master/mesh/prov-initiator.c)
- [BlueZ Mesh API](https://github.com/bluez/bluez/blob/master/doc/mesh-api.txt)
- [BlueZ provisioner tool](https://github.com/bluez/bluez/blob/master/tools/mesh/README)
- [BlueZ GATT characteristic API](https://github.com/bluez/bluez/blob/master/doc/org.bluez.GattCharacteristic.rst)

BlueZ offers Mesh message APIs and a separate phone GATT server API, but that
alone does not establish parity. A backend must also preserve exact ATT errors
(including application errors 0x80-0x84), write rejection, connection-owned
inventory reads and subscriptions, and pause/resume Mesh scanning without
disconnecting the phone or losing Mesh state. Single-controller operation and
separate stable advertising identities are not established by this inspection.

**Updated gate decision:** the user approved a BlueZ PB-ADV implementation.
No ESP32 bridge or custom Bluetooth stack is introduced. BlueZ maps invalid
scalar values to ATT unlikely-error (0x0E) instead of the ESP32's 0xFF; the user
explicitly approved this mapping. Application errors 0x80-0x84 remain distinct.
The original PB-GATT requirement below is superseded by this approval.
Permission to run checks is still separate and has not been given.

## Mesh and sensor contract

All integer payload fields are little endian. Vendor company ID is `0x1234`,
server model `0x5678`, client model `0x5679`. Gateway primary address is `0x0001`;
new node allocation starts at `0x0100`. NetKey/AppKey index is 0, TTL is 7.
Capacity is ten known nodes and ten discovery candidates. Accept discovery UUIDs
only with opaque leading bytes `AA BB`; do not reverse UUID bytes for display.

Configuration is serialized: AppKey Add, server Model App Bind, then Model
Publication Set to the gateway, TTL 7, period 0. Each request has a five-second
timeout and up to three retries after its initial attempt on transport error;
a nonzero configuration status fails the operation. Do not ACK sensor reports
while their node is still configuring. Restore known nodes and reconfigure
eligible nodes at startup, as in the source.

| Opcode selector | Payload after the three-byte vendor opcode |
| --- | --- |
| 01 / 03 / 04 | BME GET / legacy STATUS / echo ACK; STATUS and ACK use temperature i16 + humidity u16 |
| 07 / 05 / 06 | Moisture GET / legacy STATUS / echo ACK; STATUS and ACK use ADC u16 + percent u8 |
| 08 | BME REPORT: transaction u32 + temperature i16 + humidity u16; 8 bytes, or 10 with chip-temperature tail |
| 09 | Moisture REPORT: transaction u32 + ADC u16 + percent u8; 7 bytes, or 9 with chip-temperature tail |
| 0A / 0B | ACK / READY: transaction u32 + kind u8; exactly 5 bytes; kind 1 BME, 2 moisture |

Selectors 01-0B encode first opcode byte `selector | 0xC0`, followed by company
bytes `34 12`. Internal SDK opcode integers are not the on-air byte order.
Temperature is hundredths of Celsius (-4000 through 8500), humidity hundredths
of percent (0 through 10000). ADC is 0 through 4095; moisture percent is 0-100
or 255 for unknown. Optional chip temperature is signed hundredths (-4000
through 12500), with `0x8000` unavailable. Accept only original or extended
report lengths. Extended reports require Mesh segmentation.

Reject reports without the expected NetKey/AppKey indexes or a registered source
address. Decode and validate before updating state. A duplicate transaction/kind
is ACKed again but must not update readings, freshness, screen selection or
phone notifications. Legacy reports remain accepted and echoed, but do not
authorize sleep. Freshness is per module and expires at age **>= 14 seconds**.
Live values and freshness are not restored from disk after a restart.

READY must match the current report ID/kind, arrive within three seconds of the
report, and not already be armed. Expected node sleep is 12 seconds. Mesh may
pause only after 0.5 seconds settling and before 11.5 seconds after READY,
when every seen peer allows it and no configuration/provisioning/management
work or discovery requires listening. Evaluate the worker every 50 ms. Keep a
newly enabled receive window open for two seconds; retry radio errors after
one second. Reports hold the radio for at least 0.5 seconds to send their ACK.
Once a report is due, listen until a new handshake restores synchronization.
Do not disable the Bluetooth controller used by the phone.

## Phone GATT contract

Primary service `0x00A0`, complete advertised name `SRLAB_THPS_MAIN` in both
advertising and scan response. Advertise after Mesh registration and GATT service
readiness, without waiting for remote nodes or AppKey readiness. Stop phone
advertising while connected, resume after disconnect. The Pi uses its own
identity; do not impersonate the S3 address. Preserve stable phone identity
separate from nameless Mesh advertisements where the approved backend permits.

| UUID | Properties | Value |
| --- | --- | --- |
| A005 | READ, NOTIFY; CCC | Latest BME TLV followed by latest moisture TLV; max actual length 13 |
| A004 | READ, WRITE | One byte rotation 1-4; default 3 |
| A008 | READ, WRITE | One byte brightness 0-50; ESP32 default 1; Pi LCD new-install default 50 |
| A009 | WRITE with response | Management command, 4-20 bytes |
| A00A | READ | Frozen inventory captured for this phone connection |
| A00B | READ, NOTIFY; CCC | Exactly 20 status bytes |

DATA TLVs are `[type u8, length u8, value]`: type `11`, length 4, is node
address u16, temperature i8, humidity u8; type `12`, length 5, is address u16,
ADC u16, moisture u8. Round hundredths to whole units, halves away from zero
for signed temperature. No local sensor, pressure or chip-temperature TLV is
emitted. Missing readings produce absent records; DATA retains last values
when stale. READ and NOTIFY use the same snapshot. Do not notify an empty DATA
snapshot. Deletion invalidates only records belonging to the removed address.

Initial MTU is 23. Reads allow offsets through value length, with an empty
result at the exact end; larger offsets fail. Return at most MTU-1 bytes.
CAPTURE replaces only the connection's inventory buffer; subsequent reads must
slice that immutable buffer, not regenerate it. Connect/disconnect resets it
to eight zero bytes with version byte 1. Disconnect stops discovery, clears
subscriptions, and does not cancel already owned node-management work.

CCC accepts exactly two bytes, value 0 or 1. Enabling sends the current DATA
(if nonempty) or status immediately. Reset subscriptions on disconnect.
Send management notifications when encoded status changes, not every tick.

Reject all prepared and execute writes with request-not-supported. Reject
nonzero write offsets with invalid-offset. A009 additionally rejects Write
Without Response. Bad write length maps to invalid-attribute-length, unsupported
command/version to request-not-supported, and bad scalar/address/request ID to
out-of-range. A004/A008 persistence failure maps to unlikely-error and must not
apply the new setting. Preserve the reference ATT error values; do not collapse
them to generic success or failure.

## Management framing and lifecycle

Command header: version u8 = 1, opcode u8, nonzero request ID u16.

| Opcode | Command | Total length and trailing fields |
| --- | --- | --- |
| 1 | SCAN_START | 5; seconds u8 in 10-60 |
| 2 | SCAN_STOP | 4 |
| 3 | CAPTURE | 4 |
| 4 | BIND | 20; opaque UUID[16] |
| 5 | RETRY_CONFIG | 6; unicast u16 in 0001-7FFF |
| 6 | RESET | 6; same address encoding |
| 7 | FORGET | 6; same address encoding |

Identical retained request ID/bytes is idempotent, including terminal results;
reuse with different bytes is conflict `0x84`. SCAN_STOP and CAPTURE do not
replace that retained operation. Check replay before dispatching either.
Not-ready/event-loss is `0x83`; owned work or incompatible concurrent activity
is busy `0x80`; missing/ineligible target `0x81`; capacity `0x82`.
Retained cleanup blocks BIND and address reuse. ATT acceptance means queued,
not completed. Candidates expire after 30 seconds; protect the selected candidate
from replacement. Set overflow instead of silently overwriting fresh candidates.

Status bytes: 0 version, 1 opcode, 2-3 request ID, 4 state, 5 error, 6-7 address,
8 flags (bits 0-6), 9 scan seconds remaining (ceiling), 10-13 boot ID,
14-17 snapshot ID, 18-19 zero. States 0-7 are idle, queued, provisioning,
configuring, resetting, deleting, complete, failed. Errors 0-8 are none, radio,
provisioning, configuring, deadline, reset-unconfirmed, SDK-delete, cleanup,
scan-cancelled. Flags bits 0-6 are scan-requested, scan-ready,
overflow, reset-confirmed, local-removed, reset-unconfirmed, SDK-owned.

Inventory header: version at 0, count at 1, overflow at 2, zero at 3, snapshot
ID u32 at 4. Increment snapshot ID on CAPTURE and skip zero. Records are 20
bytes: state u8, module type u8, address u16, UUID[16]. Known modules precede
fresh unknown candidates; omit candidates already known. States: candidate 0,
waiting 1, online 2, sleeping 3, offline 4, needs-configuration 5. Types: unknown
0, TempHumi 1, moisture 2. Maximum buffer 408 bytes.

Deadlines: radio readiness 5 seconds; provisioning 120 seconds; configuration
65 seconds; reset 30 seconds; local deletion 10 seconds. Failed watchdog results
remain failed even if a late callback arrives. Ownership remains held until
outstanding SDK/backend callbacks and cleanup have drained. Lost worker events
fail closed; never release uncertain work or reuse its address.

RESET sends remote Node Reset first. Only a successful matching response before
failure authorizes local SDK deletion. A timeout sets reset-unconfirmed and
retains the node. FORGET explicitly authorizes local deletion without remote
reset. Local deletion success then authorizes serialized cache/storage cleanup.
Remove pending configuration, module type metadata, matching candidate, DATA
records and selected display data; serialize this against complete report
processing. Retain deletion evidence until cleanup succeeds. A later FORGET
retries cleanup without deleting the SDK node twice. Match UUID as well as
address before acting, so address reuse cannot target another physical node.

## Target Pi display and persistence behavior

The running UI must show only whole-unit BME280 temperature and humidity,
using the latest fresh accepted TempHumi report across nodes. Moisture and chip
temperature never replace it. Before a reading, after deletion of the selected
node, and at the 14-second deadline, show loading. Do not fall back to another
node's old sample. Keep the existing 64x64 framebuffer and output drivers.
Use a synchronized snapshot from the worker; no Bluetooth or disk I/O in render.

A004 rotation semantics, on the Pi's logical square: 1 identity, 2 clockwise
90 degrees, 3 180 degrees, 4 clockwise 270 degrees. Inverse-map touch if used.
A008 retains its saved 0-50 value, but the temporary LCD uses full RGB output
for every nonzero value, as requested on 2026-11-11. Zero remains off; clear once
when off and keep protocol/freshness work running. On resumes current data or
loading according to age. Physical backlight and S3 temporal dimming waveforms
are not reproduced by LCD attenuation. No demo menus or clock remain accessible
in the migrated runtime; no HTTP or tap features are introduced.

Save display settings before applying/acknowledging changed values. Unchanged
writes avoid disk I/O only when storage is available. Linux persistence
must be durable (atomic replacement plus file/directory fsync) and serialize
save/apply. New Mesh keys, device keys, IV/sequence state, address allocation,
and UUID-bound module metadata need backend-owned durable storage. Storage
errors must never erase keys, silently start a new network or reuse sequence
numbers. Sensor values/freshness are RAM-only. Existing Pi demo settings must
not clamp BLE zero brightness to the demo's 10 percent minimum.

## Checkpoints and deferred acceptance

Repository: https://github.com/kozubdmytro/rpi_rabbit.git. It had no advertised
branches at inspection, so initial history uses `main`. Initial checkpoint also
captures the existing Pi prototype, excluding ignored local artifacts/settings.
All preparation commits are explicitly unverified.

Do not invoke `build.sh` (it runs tests), `make`, CMake, app previews, test
scripts, Pi probes, deployment, service changes or pairing until authorized.
After permission, acceptance must cover protocol byte parity, management replay
and failures, reset/forget safety, legacy/extended and duplicate reports,
multi-node freshness, sleep recovery, exact ATT behavior, display loading/off/
rotation, durable restarts, PB-GATT pairing and phone/Mesh coexistence. A passing
host build cannot establish RF or hardware equivalence.
