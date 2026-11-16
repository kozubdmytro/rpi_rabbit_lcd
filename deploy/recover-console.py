#!/usr/bin/env python3
"""Restore an unowned console after an interrupted display process.

Used only by the appliance service, before startup and after process cleanup.
Refuse recovery while a process owns the console or the ILI9486 DRM card.
"""
import fcntl
import glob
import os
import subprocess

devices = ['/dev/tty0']
for card in glob.glob('/sys/class/drm/card[0-9]*'):
    if os.path.basename(os.path.realpath(card + '/device/driver')) == 'ili9486':
        devices.append('/dev/dri/' + os.path.basename(card))
owners = subprocess.run(['/usr/bin/fuser', *devices], capture_output=True)
if owners.returncode != 1:
    raise SystemExit('Console recovery refused: display still owned or ownership check failed: '
                     + owners.stdout.decode().strip() + ' ' + owners.stderr.decode().strip())
fd = os.open('/dev/tty0', os.O_RDWR | os.O_CLOEXEC)
try:
    fcntl.ioctl(fd, 0x4B3A, 0)  # KDSETMODE, KD_TEXT
finally:
    os.close(fd)
print('Unowned display console ready.')
