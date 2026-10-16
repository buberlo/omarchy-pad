#!/bin/sh
ulimit -s 8192
export FEX_DISABLETELEMETRY=1
for name in FEX-apple-m1 FEX-cortex-a76; do
    wget -q "http://192.168.0.104:8765/$name" -O "/opt/fex/$name" || exit 90
    chmod +x "/opt/fex/$name"
done
for name in FEX FEX-apple-m1 FEX-cortex-a76; do
    echo "=== $name static x86-64 ==="
    timeout 15 "/opt/fex/$name" /opt/fex/hello-x86_64
    echo "HELLO_EXIT=$?"
    echo "=== $name threads SIMD executable-memory signals ==="
    timeout 20 "/opt/fex/$name" /opt/fex/x86-runtime-smoke
    echo "RUNTIME_EXIT=$?"
done
