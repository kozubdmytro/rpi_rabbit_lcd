#!/usr/bin/env python3
"""Atomically install and persist the verified service unit before reboot."""
import os
from pathlib import Path
import subprocess
import tempfile

if os.geteuid() != 0:
    raise SystemExit('Run as root: sudo python3 deploy/install-service.py')
source = Path(__file__).resolve().with_name('smartrabbit-test.service')
data = source.read_bytes()
if len(data) < 100 or b'ExecStart=' not in data or b'[Install]' not in data:
    raise SystemExit('Refusing to install an empty or incomplete service unit')
subprocess.run(['systemd-analyze', 'verify', str(source)], check=True)
target = Path('/etc/systemd/system/smartrabbit-test.service')
fd, temporary = tempfile.mkstemp(prefix='.smartrabbit-test-', dir=target.parent)
try:
    with os.fdopen(fd, 'wb') as output:
        os.fchmod(output.fileno(), 0o644)
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, target)
    directory = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
finally:
    if os.path.exists(temporary):
        os.unlink(temporary)
if target.read_bytes() != data:
    raise SystemExit('Installed unit verification failed')
subprocess.run(['systemctl', 'daemon-reload'], check=True)
print('PASS: complete service unit atomically installed, fsynced, and verified')
