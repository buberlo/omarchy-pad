#!/bin/sh
set -eu
cd /work
ulimit -s 8192
unset LD_PRELOAD
./munmap-length-arm64 > native-control.txt 2>&1
set +e
FEX_DISABLETELEMETRY=1 FEX_ROOTFS=/work/probe-root \
LD_LIBRARY_PATH=/work/probe-root/usr/lib/x86_64-linux-gnu \
./FEX-apple-m1-original ./munmap-length-x86-low > original-low-address.txt 2>&1
original=$?
set -e
[ "$original" -eq 1 ]
grep -q "^MUNMAP_PROBE failures=15$" original-low-address.txt
FEX_DISABLETELEMETRY=1 FEX_ROOTFS=/work/probe-root \
LD_LIBRARY_PATH=/work/probe-root/usr/lib/x86_64-linux-gnu \
./FEX-apple-m1-munmap ./munmap-length-x86-low > patched-low-address.txt 2>&1
grep -q '^MUNMAP_PROBE failures=0$' native-control.txt
grep -q '^MUNMAP_PROBE failures=0$' patched-low-address.txt
[ "$(grep -c ' PASS$' native-control.txt)" -eq 18 ]
[ "$(grep -c ' PASS$' patched-low-address.txt)" -eq 18 ]
printf 'Original FEX low-address probe exited %s; patched and native probes exited 0.\n' "$original"
tail -n 1 native-control.txt original-low-address.txt patched-low-address.txt
