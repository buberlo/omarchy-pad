#!/bin/sh
set -eu
cd /work/glvnd-test
unset LD_PRELOAD
export FEX_DISABLETELEMETRY=1 FEX_ROOTFS=/opt/hollow-runtime LD_LIBRARY_PATH=/opt/hollow-runtime/usr/lib/x86_64-linux-gnu
set +e
/work/FEX-apple-m1-munmap /work/glvnd-test/repair/gl-load-probe-x86 > before.txt 2>&1
before=$?
set -e
[ "$before" -eq 3 ]
grep -q LIBGL_LOAD_FAIL before.txt
sh repair/install.sh > install.txt 2>&1
/work/FEX-apple-m1-munmap /work/glvnd-test/repair/gl-load-probe-x86 > after.txt 2>&1
grep -q LIBGL_LOAD_PASS after.txt
sh repair/install.sh > idempotent.txt 2>&1
! grep -q '^ADDED' idempotent.txt
mv /opt/hollow-runtime/usr/lib/x86_64-linux-gnu/libGL.so.1.7.0 /opt/hollow-runtime/usr/lib/x86_64-linux-gnu/libGL.so.1.7.0.test-backup
printf 'different library' > /opt/hollow-runtime/usr/lib/x86_64-linux-gnu/libGL.so.1.7.0
set +e
sh repair/install.sh > refuse-existing.txt 2>&1
refuse=$?
set -e
mv /opt/hollow-runtime/usr/lib/x86_64-linux-gnu/libGL.so.1.7.0.test-backup /opt/hollow-runtime/usr/lib/x86_64-linux-gnu/libGL.so.1.7.0
[ "$refuse" -ne 0 ]
grep -q 'refusing to replace existing file' refuse-existing.txt
printf 'GLVND local FEX load: baseline missing, repaired PASS; idempotence PASS; conflicting-file refusal PASS.\n'
cat before.txt after.txt
