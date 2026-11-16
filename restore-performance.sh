#!/bin/sh
# systemd fallback if the process is killed before its RAII cleanup can run.
set -eu
state=/run/smartrabbit-test/previous-governor
[ -r "$state" ] || exit 0
IFS= read -r governor < "$state"
case "$governor" in
    ondemand|performance|powersave|schedutil|conservative|userspace)
        printf '%s\n' "$governor" > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
        rm -- /run/smartrabbit-test/previous-governor ;;
    *) echo 'Invalid saved CPU governor; refusing restoration.' >&2; exit 1 ;;
esac
