#!/bin/bash
# Flash a uf2 onto the GCMCE from the Pi via the serial `bootloader` command. Usage: gcmce-pi-flash.sh <file.uf2>
set -u
P=/dev/serial/by-id/usb-FlipperMCE_GC_Memory_Card_E6609CB2D31C6629-if00
uf2="$1"
python3 ~/mcecmd.py status "$P" | grep -q "mode=mass" && { echo "leaving drive mode first: $(python3 ~/mcecmd.py card "$P")"; sleep 12; }
echo "bootloader: $(python3 ~/mcecmd.py bootloader "$P")"
dev=""; for i in $(seq 1 40); do dev=$(ls /dev/disk/by-id/usb-RPI_RP2_* 2>/dev/null | grep -- '-part1$' | head -1); [ -n "$dev" ] && break; sleep 0.5; done
[ -n "$dev" ] || { echo "FAIL: RPI-RP2 never appeared"; exit 1; }
sudo mkdir -p /mnt/rp2 && sudo mount "$dev" /mnt/rp2 && cat /mnt/rp2/INFO_UF2.TXT | head -2 && sudo cp "$uf2" /mnt/rp2/ && sync; sudo umount /mnt/rp2 2>/dev/null
for i in $(seq 1 40); do [ -e "$P" ] && break; sleep 0.5; done; sleep 1
echo "after flash: $(python3 ~/mcecmd.py version "$P") / $(python3 ~/mcecmd.py status "$P")"
