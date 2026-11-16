#!/bin/sh
set -eu
cd "$(dirname "$0")"
make -j4
make test
