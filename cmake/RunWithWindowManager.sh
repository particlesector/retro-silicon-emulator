#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run inside xvfb-run. SDL's initial X11 mapping can hang if it races the WM.
set -eu
state=$(mktemp -d /tmp/rse-wm.XXXXXXXX)
wm_pid=
cleanup() {
    result=$?
    trap - EXIT
    if [ -n "$wm_pid" ]; then
        kill "$wm_pid" 2>/dev/null || true
        wait "$wm_pid" 2>/dev/null || true
    fi
    rm -f "$state/ready" "$state/openbox.log"
    rmdir "$state"
    exit "$result"
}
trap cleanup EXIT
# Openbox runs --startup after initialization, including claiming WM ownership.
# The marker path is generated from the fixed, shell-safe /tmp template above.
openbox --startup "touch '$state/ready'" >"$state/openbox.log" 2>&1 &
wm_pid=$!
attempt=0
while [ ! -f "$state/ready" ]; do
    if ! kill -0 "$wm_pid" 2>/dev/null || [ "$attempt" -ge 100 ]; then
        cat "$state/openbox.log" >&2
        echo 'Window manager did not become ready' >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done
"$@"
