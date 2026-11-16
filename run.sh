#!/bin/sh
set -eu
cd "$(dirname "$0")"
for arg in "$@"; do
    if [ "$arg" = "--headless" ] || [ "$arg" = "--help" ]; then
        exec ./build/smartrabbit-test "$@"
    fi
done
if [ "$(id -u)" -ne 0 ]; then
    exec sudo ./build/smartrabbit-test "$@"
fi
exec ./build/smartrabbit-test "$@"
