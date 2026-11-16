#!/usr/bin/env python3
"""Send one command to the FlipperMCE serial port and print the reply line.
Usage: mcecmd.py <command> [port]   (port default: the first /dev/cu.usbmodem* or /dev/ttyACM*)"""
import glob, os, sys, termios, time

def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/serial/by-id/usb-FlipperMCE*")) + sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no FlipperMCE serial port found")
    return ports[0]

def main():
    cmd = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else find_port()
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0; attrs[1] = 0; attrs[3] = 0          # raw: no input/output/local processing
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[6][termios.VMIN] = 0; attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)       # opening the port raises DTR
    time.sleep(0.2)
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, (cmd + "\r\n").encode())
    buf, end = b"", time.time() + 3
    while time.time() < end and b"\n" not in buf:
        try:
            buf += os.read(fd, 256)
        except BlockingIOError:
            time.sleep(0.02)
    os.close(fd)
    print(buf.decode(errors="replace").strip() or "(no reply)")

main()
