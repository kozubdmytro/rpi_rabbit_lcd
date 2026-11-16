# Raspberry Pi Mesh gateway

`gateway.*` connects these helpers to the serialized protocol worker and durable
journal. `bluez.*` exports the Mesh application and phone GATT objects on the
system bus, using BlueZ's PB-ADV provisioning. The UI consumes only `View` through
a short mutex-protected snapshot. See [setup and limitations](../../BLE_SETUP.md).
This implementation has not been built, tested, installed or paired.

These headers are Pi-side C++17 copies of the current ESP32 project's pure
protocol/scheduling/management helpers. See [the migration contract](../../BLE_MIGRATION.md)
and [source fingerprints](../../migration/esp32-source-sha256.json).

| Header | Prepared behavior |
| --- | --- |
| sensor_protocol.h | Vendor/model IDs, BME encoding and validation |
| moisture_protocol.h | Moisture encoding and validation |
| chip_temperature_protocol.h | Original/extended report length and optional tail validation |
| duty_protocol.h | Transaction IDs, REPORT/READY state, receive-pause eligibility |
| node_freshness.h | Independent 16-second freshness predicate |
| management_protocol.h | Commands, validation, status byte encoding |
| management_state.h | Replay, deadlines, discovery candidates, reset/delete/cleanup state helpers, frozen read slicing |

Port changes are limited to replacing the SDK opcode macro with a local
equivalent and rewriting two C-only initializers as zero-initialized C++17
objects followed by assignments. Field encodings, constants, predicates and
state transitions are retained. Original symbols are retained for comparison.
Callers must zero-initialize state, validate inputs, serialize access and supply
monotonic microsecond timestamps. These helpers do not own threads or storage.

`management_reset_result` returning true authorizes a deletion request; it does
not itself delete a node. The gateway journals intent before issuing backend
deletion and removes metadata only after the backend completes. Backend calls
and callbacks remain on the worker thread. No fabricated sensor values are used.
