#!/bin/sh
# Fails when an image tar contains two paths that differ only in case.
#
# The iPad app container and the Mac build volume are case-insensitive, so two such paths
# share one backing file in the fakefs, and the importer silently drops the second one
# (tools/fakefs.c continues on the EISDIR it gets from the first). Arch's tree is far
# larger than Alpine's, so this is checked before an image is ever shipped.
#
# A few collisions are known and harmless: legacy Linux uapi headers that ship both cases
# of the same name (they are kernel header aliases nothing in the session includes). They
# are listed in the allowlist below and reported, not failed. Everything else fails.
#
#   sh installation/omarchy/check-case-collisions.sh image.tar.gz
set -eu

TAR=${1:?usage: check-case-collisions.sh IMAGE.tar.gz}

# Known-benign pairs, lowercase-compared, from linux-api-headers. Keep this list short and
# justified: anything added here is a path that will silently not exist in the guest.
ALLOW='usr/include/linux/netfilter/xt_tcpmss.h
usr/include/linux/netfilter/xt_dscp.h
usr/include/linux/netfilter_ipv6/ip6t_hl.h'

collisions=$(tar -tzf "$TAR" | sed 's:/$::' | awk '
    { key = tolower($0); if (key in seen && seen[key] != $0) print seen[key] " <> " $0; else seen[key] = $0 }
')

blocking=0
allowed=0
printf '%s\n' "$collisions" | while IFS= read -r pair; do
    [ -n "$pair" ] || continue
    key=$(printf '%s' "$pair" | sed 's/ <> .*//' | sed 's:^\./::' | tr 'A-Z' 'a-z')
    if printf '%s\n' "$ALLOW" | grep -qxF "$key"; then
        printf 'allowed: %s\n' "$pair"
    else
        printf 'COLLISION: %s\n' "$pair" >&2
    fi
done

blocking=$(printf '%s\n' "$collisions" | while IFS= read -r pair; do
    [ -n "$pair" ] || continue
    key=$(printf '%s' "$pair" | sed 's/ <> .*//' | sed 's:^\./::' | tr 'A-Z' 'a-z')
    printf '%s\n' "$ALLOW" | grep -qxF "$key" || printf 'x\n'
done | wc -l | tr -d ' ')

if [ "$blocking" != 0 ]; then
    echo "case collisions in $TAR: $blocking blocking (see above)" >&2
    exit 1
fi

echo "no blocking case collisions in $TAR ($(tar -tzf "$TAR" | wc -l | tr -d ' ') entries)"
