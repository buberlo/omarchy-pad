#!/bin/sh
# 99: put the compositor that came with the Linux system back. Needs another desktop session
# restart afterwards (Quick Settings) to take effect; until then the patched binary keeps
# running in memory.
set -u
if [ ! -f /tmp/ishwl-before-kb4453 ]; then
    echo "NO_BACKUP (/tmp/ishwl-before-kb4453 is missing)"
    exit 1
fi
ls -l /tmp/ishwl-before-kb4453
sha256sum /tmp/ishwl-before-kb4453
# mv, not cp: overwriting a running executable fails with "text file busy"
cp -f /tmp/ishwl-before-kb4453 /usr/local/bin/ishwl.restore || exit 1
mv -f /usr/local/bin/ishwl.restore /usr/local/bin/ishwl || exit 1
echo "installed again:"
ls -l /usr/local/bin/ishwl
sha256sum /usr/local/bin/ishwl
if cmp -s /usr/local/bin/ishwl /tmp/ishwl-before-kb4453; then
    echo "RESTORED=1 (the original binary is in place; restart the desktop session)"
else
    echo "RESTORE_FAILED=1"
fi
