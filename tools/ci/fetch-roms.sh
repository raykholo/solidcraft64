#!/bin/sh
# Fetch the C64 and 1541 ROMs from the official VICE 3.7.1 source release into
# $1 (default /usr/share/vice). Debian and Ubuntu leave them out of their vice
# package. They are used by the emulator at test time only: never commit them,
# never put them in a release.
set -eu
dest=${1:-/usr/share/vice}
tmp=$(mktemp -d)
url=https://downloads.sourceforge.net/project/vice-emu/releases/vice-3.7.1.tar.gz
sum=7e3811e6024db0698bfbc321bb324572446b8853d01b4073f09865957b0cab98
if [ -n "${VICE_TGZ:-}" ]; then cp "$VICE_TGZ" "$tmp/vice.tgz"; else curl -fsSL --retry 3 -o "$tmp/vice.tgz" "$url"; fi
echo "$sum  $tmp/vice.tgz" | sha256sum -c -
tar -xzf "$tmp/vice.tgz" -C "$tmp" --wildcards 'vice-3.7.1/data/C64/*' 'vice-3.7.1/data/DRIVES/*'
mkdir -p "$dest/C64" "$dest/DRIVES"
cp "$tmp"/vice-3.7.1/data/C64/kernal-* "$tmp"/vice-3.7.1/data/C64/basic-* "$tmp"/vice-3.7.1/data/C64/chargen-* "$dest/C64/"
cp "$tmp"/vice-3.7.1/data/DRIVES/dos1541* "$dest/DRIVES/"
rm -rf "$tmp"
ls "$dest/C64" "$dest/DRIVES" | grep -E 'kernal|basic|chargen|dos1541'
