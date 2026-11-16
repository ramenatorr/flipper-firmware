#!/bin/bash
# One drive-mode round trip on the Mac. Usage: mac-cycle-test.sh <exit: card|eject|timeout>
set -u
how="$1"; T="$(dirname "$0")"; P=/dev/cu.usbmodem2101
cmd() { perl -e 'alarm 8; exec @ARGV' python3 "$T/mcecmd.py" "$1" "$P"; }
want_hash=13f0533b55d3cfe8c8491e014fd7c265f1c0358ecece04435d57befa136df3bf
f="/Volumes/NO NAME/MemoryCards/GC/DL-DOL-GKQJ-JPN/DL-DOL-GKQJ-JPN-1.raw"
echo "before: $(cmd status)"
t0=$(date +%s); echo "mass: $(cmd mass)"
for i in $(seq 1 60); do [ -f "$f" ] && break; sleep 0.5; done
[ -f "$f" ] || { echo "FAIL: drive never mounted"; exit 1; }
echo "mounted after $(( $(date +%s)-t0 )) s; status: $(cmd status)"
h=$(shasum -a 256 "$f" | cut -d' ' -f1); [ "$h" = "$want_hash" ] && echo "save hash matches" || echo "FAIL hash $h"
t1=$(date +%s)
case "$how" in
  card)    diskutil unmount "/Volumes/NO NAME" >/dev/null && echo "unmounted; card: $(cmd card)";;
  eject)   d=$(diskutil info "/Volumes/NO NAME" | awk '/Part of Whole/{print $4}'); diskutil eject "$d" && echo "host eject sent ($d)";;
  timeout) diskutil unmount "/Volumes/NO NAME" >/dev/null && echo "unmounted; waiting for the no-host timeout is not possible on a live host (stays mounted at USB level)";;
esac
gone=""; for i in $(seq 1 60); do sleep 0.5; [ -e "$P" ] || gone=1; [ -n "$gone" ] && [ -e "$P" ] && break; done
sleep 1
echo "port back=${gone:+yes} after $(( $(date +%s)-t1 )) s; after: $(cmd status)"
