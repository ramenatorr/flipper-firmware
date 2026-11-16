#!/bin/bash
set -u
P=/dev/serial/by-id/usb-FlipperMCE_GC_Memory_Card_E6609CB2D31C6629-if00
cmd() { python3 ~/mcecmd.py "$1" "$P"; }
dev=/dev/disk/by-id/usb-FlipMCE_SD_card_E6609CB2D31C6629-0:0-part1
cmd status | grep -q mode=mass || { echo "mass: $(cmd mass)"; for i in $(seq 1 60); do [ -e $dev ] && break; sleep 0.5; done; sleep 2; }
sudo mkdir -p /mnt/gcmce && sudo mount -o uid=$(id -u),gid=$(id -g) "$dev" /mnt/gcmce || { echo "FAIL mount: $(cmd status)"; exit 1; }
t=$(date +%s.%N); sha256sum /mnt/gcmce/MemoryCards/GC/*/*.raw; echo "hash took $(awk "BEGIN{printf \"%.1f\", $(date +%s.%N)-$t}") s"
head -c 65536 /dev/urandom > /tmp/probe.bin && cp /tmp/probe.bin /mnt/gcmce/MemoryCards/zz-pi-probe.bin && sync && echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null && cmp /tmp/probe.bin /mnt/gcmce/MemoryCards/zz-pi-probe.bin && echo "write+readback OK" && rm /mnt/gcmce/MemoryCards/zz-pi-probe.bin && sync
sudo umount /mnt/gcmce && echo unmounted
echo "stats: $(cmd status)"
t=$(date +%s.%N); echo "card: $(cmd card)"; gone=""; for i in $(seq 1 80); do sleep 0.25; [ -e "$P" ] || gone=1; [ -n "$gone" ] && [ -e "$P" ] && break; done; sleep 1
echo "back=${gone:+yes} after $(awk "BEGIN{printf \"%.1f\", $(date +%s.%N)-$t}") s: $(cmd status)"
journalctl -k --since -3min --no-pager | grep -c "I/O error"
