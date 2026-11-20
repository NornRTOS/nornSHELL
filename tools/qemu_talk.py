#!/usr/bin/env python3
"""Talk to nosh in Espressif's QEMU: send command lines, print what comes back.

Usage: qemu_talk.py IMAGE.bin [--seconds N] [--expect TEXT]... [--send LINE]... [--nic SPEC] [--line-delay S]
                    [--flash FILE] [--prompt TEXT] [--flash-image FILE@OFFSET]

Builds the 4 MB flash image as nornCore's tools/qemu_run.py does (IMAGE at 0x1000),
starts qemu-system-xtensa with the serial port on a local TCP socket, waits for the
first prompt, sends every --send LINE followed by CR (in order, each after the next
prompt), and prints the whole conversation. With
--expect, the exit code is 0 only if every TEXT occurred in the output; without it, 0 if
the prompt was seen at all. QEMU_ESP points at the QEMU installation.

--flash FILE keeps the 4 MB flash in FILE instead of a temporary one: the file is created
if missing, the image is written into it at 0x1000 before every run, and whatever the
program wrote to the flash (a configuration store) is still there at the next run, which
is therefore a reboot. --prompt TEXT waits for TEXT instead of "nosh> " (an application
without a shell prints a line when it is done).
"""
import argparse, os, socket, subprocess, sys, tempfile, time

PROMPT = b"nosh> "

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--seconds", type=float, default=20.0)
ap.add_argument("--expect", action="append", default=[])
ap.add_argument("--settle", type=float, default=0.5, help="seconds to wait after the last line")
ap.add_argument("--send", action="append", default=[], dest="lines", metavar="LINE")
ap.add_argument("--flash", help="persistent flash file (default: a fresh temporary one)")
ap.add_argument("--prompt", default="nosh> ", help="text to wait for before sending")
ap.add_argument("--flash-image", metavar="FILE@OFFSET",
                help="code-in-flash image and its offset (default: <image>.flash.bin at 0x80000 if present)")
ap.add_argument("--nic", metavar="SPEC", help="QEMU network device, e.g. user,model=open_eth")
ap.add_argument("--line-delay", type=float, default=0.0,
                help="seconds to wait before each line is sent (for a target that is busy after the previous one)")
ap.add_argument("--char-delay", type=float, default=0.002,
                help="seconds between characters, as a terminal delivers them (0: whole line at once)")
args = ap.parse_args()
PROMPT = args.prompt.encode()

qemu = os.path.join(os.environ.get("QEMU_ESP", os.path.expanduser("~/dev/tools/qemu-esp")),
                    "bin", "qemu-system-xtensa")

with tempfile.TemporaryDirectory() as tmp:
    flash = args.flash or os.path.join(tmp, "flash.bin")
    if args.flash and os.path.exists(flash):
        data = bytearray(open(flash, "rb").read())
        if len(data) != 4 * 1024 * 1024:
            data = bytearray(b"\xff" * (4 * 1024 * 1024))
    else:
        data = bytearray(b"\xff" * (4 * 1024 * 1024))
    img = open(args.image, "rb").read()
    data[0x1000:0x1000 + len(img)] = img
    # The build's code-in-flash image (nornos_<app>.flash.bin, FLASH_BASE 0x80000 unless
    # given otherwise) goes in too, as nornCore's qemu_run.py does; without it an
    # application with FLASH_OBJS faults at its first flash function.
    stem = args.image[:-4] if args.image.endswith(".bin") else args.image
    extra = args.flash_image or (stem + ".flash.bin@0x80000" if os.path.exists(stem + ".flash.bin") else None)
    if extra:
        path, offset = extra.rsplit("@", 1)
        blob = open(path, "rb").read()
        offset = int(offset, 0)
        data[offset:offset + len(blob)] = blob
    open(flash, "wb").write(data)

    # Let the kernel pick a free port, hand it to QEMU as a server socket.
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    cmd = [qemu, "-M", "esp32", "-display", "none", "-monitor", "none",
           "-serial", "tcp:127.0.0.1:%d,server,nowait" % port,
           "-drive", "file=%s,if=mtd,format=raw" % flash]
    if args.nic:
        cmd += ["-nic", args.nic]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = bytearray()
    t0 = time.time()
    try:
        sock = None
        while sock is None and time.time() - t0 < args.seconds:
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            except OSError:
                time.sleep(0.1)
        if sock is None:
            print("qemu_talk: could not connect to QEMU's serial port")
            sys.exit(2)
        sock.settimeout(0.1)

        def pump(until=None, limit=None):
            """Read until `until` appears (counted from the current end) or time runs out."""
            start = len(out)
            deadline = time.time() + (limit if limit is not None else args.seconds)
            while time.time() < deadline:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    out.extend(chunk)
                    if until is not None and until in out[start:]:
                        return True
                except socket.timeout:
                    if until is None:
                        return True
            return until is None

        if not pump(PROMPT):
            print(out.decode("utf-8", "replace"))
            print("qemu_talk: no prompt within %.0f s" % args.seconds)
            sys.exit(1)
        for line in args.lines:
            if args.line_delay > 0:
                time.sleep(args.line_delay)
            # A terminal delivers a line at typing speed, or at the baud rate for a paste;
            # the whole line at once would overrun a small receive queue in the target.
            for ch in line.encode() + b"\r":
                sock.sendall(bytes([ch]))
                if args.char_delay > 0:
                    time.sleep(args.char_delay)
            if not pump(PROMPT, limit=5.0):
                print(out.decode("utf-8", "replace"))
                print("qemu_talk: no prompt after %r" % line)
                sys.exit(1)
        time.sleep(args.settle)
        pump(None, limit=0.3)
    finally:
        proc.kill()
        proc.wait()

text = out.decode("utf-8", "replace")
sys.stdout.write(text)
if not text.endswith("\n"):
    print()
missing = [e for e in args.expect if e not in text]
for e in missing:
    print("qemu_talk: missing %r" % e)
sys.exit(1 if missing else 0)
