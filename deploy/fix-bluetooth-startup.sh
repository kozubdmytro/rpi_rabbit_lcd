#!/bin/sh
# Run as root on the Pi: D-Bus name ownership precedes Mesh API readiness.
set -eu
install -d /usr/local/libexec /etc/systemd/system/bluetooth-mesh.service.d /etc/systemd/system/smartrabbit-test.service.d
python3 - <<'PY'
import os
from pathlib import Path


def save(path, text, mode=0o644):
    target = Path(path)
    temporary = target.with_name(target.name + '.tmp')
    with temporary.open('w') as stream:
        os.fchmod(stream.fileno(), mode)
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, target)
    directory = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


save('/usr/local/libexec/smartrabbit-wait-bluetooth', '''#!/usr/bin/python3
import subprocess
import sys
import time

mode = sys.argv[1]
if mode == 'controller':
    command = ['busctl', '--system', '--timeout=1', 'set-property',
               'org.bluez', '/org/bluez/hci0', 'org.bluez.Adapter1',
               'Powered', 'b', 'true']
elif mode == 'mesh':
    command = ['busctl', '--system', '--timeout=1', '--no-legend',
               'introspect', 'org.bluez.mesh', '/org/bluez/mesh',
               'org.bluez.mesh.Network1']
else:
    sys.exit('Unknown Bluetooth readiness mode')

deadline = time.monotonic() + 8
while time.monotonic() < deadline:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=2)
        if result.returncode == 0 and (mode == 'controller' or any(
                line.split()[:2] == ['.Attach', 'method']
                for line in result.stdout.splitlines())):
            sys.exit(0)
    except subprocess.TimeoutExpired:
        pass
    time.sleep(0.2)
sys.exit('Bluetooth readiness timeout: ' + mode)
''', 0o755)
save('/etc/systemd/system/bluetooth-mesh.service.d/srlab-startup.conf',
     '[Unit]\nAfter=bluetooth.service\nRequires=bluetooth.service\n'
     '[Service]\n'
     'ExecStartPre=/usr/local/libexec/smartrabbit-wait-bluetooth controller\n')
save('/etc/systemd/system/smartrabbit-test.service.d/bluetooth-ready.conf',
     '[Service]\n'
     'ExecStartPre=/usr/local/libexec/smartrabbit-wait-bluetooth mesh\n')
PY
sync
systemctl stop smartrabbit-test.service
systemctl daemon-reload
systemctl restart bluetooth-mesh.service
systemctl start smartrabbit-test.service
