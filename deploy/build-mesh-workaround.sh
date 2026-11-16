#!/bin/sh
# Compile only the Mesh daemon; do not install it or run tests.
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work_dir="$project_dir/.local/bluez-mesh-scan-tx"
source_dir="$work_dir/bluez-5.82"
archive_base=https://archive.raspberrypi.com/debian/pool/main/b/bluez
mkdir -p "$work_dir"
cd "$work_dir"

if [ ! -f bluez_5.82.orig.tar.gz ]; then
    curl --fail --location "$archive_base/bluez_5.82.orig.tar.gz" -o bluez_5.82.orig.tar.gz
fi
if [ ! -f bluez_5.82-1.1+rpt2.debian.tar.xz ]; then
    curl --fail --location "$archive_base/bluez_5.82-1.1+rpt2.debian.tar.xz" -o bluez_5.82-1.1+rpt2.debian.tar.xz
fi
# Source archive checksums from the distribution's 5.82-1.1+rpt2 .dsc.
printf '%s\n' \
    '818e2bfa64d3d26b49cc857448f00f4fc7bb76949266de8d7f4c07f8057bf95c  bluez_5.82.orig.tar.gz' \
    '4cfbce4fc558cdbbf2eb919018d4b940830031bf1873845c17879dce2cd41333  bluez_5.82-1.1+rpt2.debian.tar.xz' | sha256sum --check --status

# Re-extract the pinned source before applying the distribution and local patches.
tar -xzf bluez_5.82.orig.tar.gz
tar -xJf bluez_5.82-1.1+rpt2.debian.tar.xz -C "$source_dir"
cd "$source_dir"
while IFS= read -r distribution_patch; do
    case "$distribution_patch" in ''|'#'*) continue ;; esac
    patch --batch --forward -p1 < "debian/patches/$distribution_patch"
done < debian/patches/series
patch --batch --forward -p1 < "$project_dir/deploy/bluez-5.82-mesh-scan-tx.patch"

./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
    --enable-mesh --enable-external-ell --disable-maintainer-mode \
    --disable-obex --disable-client --disable-tools --disable-monitor \
    --disable-cups --disable-udev --disable-systemd --disable-manpages \
    --disable-datafiles --disable-testing --disable-test
make -j4 mesh/bluetooth-meshd
printf 'Built (unverified): %s/mesh/bluetooth-meshd\n' "$source_dir"
