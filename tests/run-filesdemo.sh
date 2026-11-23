#!/bin/sh
# Run the files demo in VICE (the sc64-vice image), then check the disk on the host.
# Needs: make filesdemo (cc65), podman, the sc64-vice image (tools/ci/Containerfile).
set -e
cd "$(dirname "$0")/.."
make filesdemo
cp build/filesdemo.d64 build/filesdemo-run.d64
timeout 300 podman run --rm --init -v "$PWD":/w:Z -w /w sc64-vice tools/ci/vice-run -warp \
    -autostart build/filesdemo-run.d64 -limitcycles "${CYCLES:-300000000}" \
    -exitscreenshot build/filesdemo.png || true
python3 tests/check_filesdemo.py build/filesdemo-run.d64
