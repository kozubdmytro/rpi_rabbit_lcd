#!/bin/sh
# Read-only inspection; no package or configuration changes.
printf '\n=== SYSTEM ===\n'
hostname
uname -a
cat /etc/os-release
tr -d '\000' < /proc/device-tree/model
printf '\n'
id
free -h
printf '\n=== DISPLAY DEVICES ===\n'
ls -l /dev/dri /dev/fb* 2>/dev/null
for f in /sys/class/graphics/fb*/name /sys/class/graphics/fb*/virtual_size /sys/class/graphics/fb*/bits_per_pixel /sys/class/drm/card*-*/status /sys/class/drm/card*-*/modes; do
    [ ! -f "$f" ] || { echo "$f"; cat "$f"; }
done
printf '\n=== DISPLAY CONFIG ===\n'
for f in /boot/firmware/config.txt /boot/config.txt /proc/cmdline; do
    [ ! -f "$f" ] || { echo "$f"; cat "$f"; }
done
printf '\n=== INPUT DEVICES ===\n'
cat /proc/bus/input/devices
ls -l /dev/input /dev/input/by-path 2>/dev/null
printf '\n=== TOOLS AND PACKAGES ===\n'
command -v c++ cmake make pkg-config fbset modetest
dpkg-query -W -f='${binary:Package} ${Version}\n' build-essential g++ cmake make libsdl2-dev libdrm-dev 2>/dev/null
printf '\n=== SESSION ===\n'
printf 'DISPLAY=%s WAYLAND_DISPLAY=%s\n' "$DISPLAY" "$WAYLAND_DISPLAY"
ps -eo comm | sort -u
printf '\n=== SUDO ===\n'
sudo -n true && echo 'Passwordless sudo available'
exit 0
