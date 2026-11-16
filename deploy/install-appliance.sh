#!/bin/sh
set -eu
if [ "$(id -u)" -ne 0 ]; then exec sudo sh "$0" "$@"; fi
cd "$(dirname "$0")/.."
systemctl stop smartrabbit-test
python3 deploy/install-service.py
systemctl daemon-reload
systemctl reset-failed smartrabbit-test
systemctl enable smartrabbit-test
sync
echo 'Smart Rabbit appliance startup installed. SSH is unchanged.'
