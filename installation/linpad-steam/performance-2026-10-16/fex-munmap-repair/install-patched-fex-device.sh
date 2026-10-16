#!/bin/sh
set -eu
BASE=http://192.168.0.104:8766
OUT=/tmp/patched-fex-validation.txt
wget -T 25 -qO /opt/fex/FEX-apple-m1-munmap.download "$BASE/FEX-apple-m1-munmap"
printf '%s  %s\n' 343c8c29bfb933d317ffea15bbb898f58bcdeb82911461a70b5235afb9600e0d /opt/fex/FEX-apple-m1-munmap.download | sha256sum -c -
chmod 755 /opt/fex/FEX-apple-m1-munmap.download
mv /opt/fex/FEX-apple-m1-munmap.download /opt/fex/FEX-apple-m1-munmap
wget -T 8 -qO /tmp/munmap-length-x86-low "$BASE/munmap-length-x86-low"
printf '%s  %s\n' 913c6ca016e2bf2922e4a4c424a070c3b5b1cd355cb0ca4cedcbf5f95664e3fe /tmp/munmap-length-x86-low | sha256sum -c -
chmod +x /tmp/munmap-length-x86-low
{
 date
 unset LD_PRELOAD
 export FEX_DISABLETELEMETRY=1 FEX_ROOTFS=/opt/hollow-runtime LD_LIBRARY_PATH=/opt/hollow-runtime/usr/lib/x86_64-linux-gnu
 /opt/fex/FEX-apple-m1-munmap /tmp/munmap-length-x86-low
 echo PATCHED_FEX_DEVICE_PASS
} > "$OUT" 2>&1
wget -T 8 -q --post-file="$OUT" "$BASE/report" -O /dev/null
tail -25 "$OUT"
