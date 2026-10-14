#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM-1 rescue: put a firmware back on an M-VAVE FM-1 that no installer reaches any more, through the chip's
mask-ROM bootloader. For an FM-1 whose firmware still plays but cannot be updated over USB-MIDI (the case that
started this: a firmware whose MIDI out over USB broke), or that hangs before its own rescue mode.

  sudo python3 fm1_rescue.py sloop            a firmware from the FM-1 Firmware Hub, by its id
  sudo python3 fm1_rescue.py some.fwsc        a package file (Felucca's format: what the hub installs)
  options: --check (look, write nothing), --yes (do not ask), --device /dev/sgN

How (each step checks before the next):
 1. The FM-1 goes to the bootloader of its AC791N chip (mask ROM, cannot be overwritten): the hub page's
    "Send to the bootloader" button, or this script itself over USB-MIDI. It shows up as a USB disk "UBOOT1.00".
 2. The ROM can only write RAM and jump: a small flash loader goes into RAM first (wl82loader.bin from
    jl-uboot-tool, MIT, by kagaimiq; downloaded once, checked by its SHA-256; it is JieLi's, not ours to ship).
 3. The flash chip must answer as the FM-1's (0x856014), the chip key is read from the FM-1's own flash and the
    package's app area must decrypt with it (as Felucca's update loader checks).
 4. Only the app area (0x4000..0x93000) is written, only the 4 KiB sectors that differ, each read back; the head
    of the flash (the SPL, the chip key) is never touched. Then the FM-1 restarts into the firmware.

Linux only (the SCSI pass-through, /dev/sg*); needs root for the bootloader's disk. Python 3, nothing else.
The protocol after jl-uboot-tool (https://github.com/kagaimiq/jl-uboot-tool, MIT, Andrey Grigoryev): the CDB
layouts and the WL82's UBOOT1.00 behaviour."""
import argparse, ctypes, fcntl, glob, hashlib, json, os, struct, sys, time, urllib.request
from pathlib import Path

HUB = "https://thefirmwarehub.com"
JL_COMMIT = "adb3f18889e88ac512ce0a3c4d8cc3d3cb30696a"
JL_URL = f"https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/{JL_COMMIT}/data/loaderblobs/usb/wl82loader.bin"
JL_SHA256 = "d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376"
JL_ADDR, JL_ARG = 0x01C02000, 0x0001             # where it runs; its argument: SPI NOR flash
APP_LO, APP_HI, SEC, CH = 0x4000, 0x93000, 0x1000, 256
FLASH_ID = bytes([0x85, 0x60, 0x14])               # the FM-1's flash chip (JEDEC)
UBOOT_KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])
LOADER_MARK = b"FELUCCA-LOADER-1"


def die(msg, code=1):
    print("fm1_rescue: " + msg, file=sys.stderr)
    sys.exit(code)


# ------------------------------------------------------------ the ciphers ---

def crc16(data, crc=0):
    """CRC-16/XMODEM (poly 0x1021): the package, the flash records and the bootloader all use it"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def enc(buf, off, size, key=0xFFFF):
    """JieLi ENC stream cipher (in place)"""
    for i in range(size):
        buf[off + i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF


def sfc(buf, size, key):
    """the flash's SFC cipher: ENC per 32-byte block, the key mixed with the block's offset"""
    for i in range(0, size, 32):
        enc(buf, i, min(size - i, 32), key ^ (i >> 2))


_MAGIC = "孟黎我爱你，玉林".encode("gb2312")


def mengli(data):
    """the CRC cipher of the WL82's UBOOT1.00 memory reads / writes (jl-uboot-tool's jl_crc_cipher; its own inverse)"""
    out = bytearray(data)
    crc = crc16(b"\xff\xff", 0xFFFF)
    for i in range(len(out)):
        crc = crc16(_MAGIC[i % len(_MAGIC):i % len(_MAGIC) + 1], crc)
        out[i] ^= crc & 0xFF
    return bytes(out)


# --------------------------------------------------- SCSI pass-through (Linux) ---

class SgIoHdr(ctypes.Structure):
    _fields_ = [("interface_id", ctypes.c_int), ("dxfer_direction", ctypes.c_int), ("cmd_len", ctypes.c_ubyte),
                ("mx_sb_len", ctypes.c_ubyte), ("iovec_count", ctypes.c_short), ("dxfer_len", ctypes.c_uint),
                ("dxferp", ctypes.c_void_p), ("cmdp", ctypes.c_void_p), ("sbp", ctypes.c_void_p),
                ("timeout", ctypes.c_uint), ("flags", ctypes.c_uint), ("pack_id", ctypes.c_int),
                ("usr_ptr", ctypes.c_void_p), ("status", ctypes.c_ubyte), ("masked_status", ctypes.c_ubyte),
                ("msg_status", ctypes.c_ubyte), ("sb_len_wr", ctypes.c_ubyte), ("host_status", ctypes.c_ushort),
                ("driver_status", ctypes.c_ushort), ("resid", ctypes.c_int), ("duration", ctypes.c_uint),
                ("info", ctypes.c_uint)]


class ScsiError(Exception):
    pass


class Sg:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR)

    def close(self):
        os.close(self.fd)

    def run(self, cdb, out=None, inlen=0, timeout_ms=10000):
        h = SgIoHdr()
        h.interface_id = ord("S")
        h.timeout = timeout_ms
        sense = ctypes.create_string_buffer(32)
        h.mx_sb_len, h.sbp = 32, ctypes.addressof(sense)
        cmd = ctypes.create_string_buffer(bytes(cdb), len(cdb))
        h.cmd_len, h.cmdp = len(cdb), ctypes.addressof(cmd)
        if out is not None:
            buf = ctypes.create_string_buffer(bytes(out), len(out))
            h.dxfer_direction, h.dxfer_len = -2, len(out)
        elif inlen:
            buf = ctypes.create_string_buffer(inlen)
            h.dxfer_direction, h.dxfer_len = -3, inlen
        else:
            buf = None
            h.dxfer_direction = -1
        if buf is not None:
            h.dxferp = ctypes.addressof(buf)
        fcntl.ioctl(self.fd, 0x2285, h)                 # SG_IO
        if h.info & 1:
            raise ScsiError(f"transfer failed (host {h.host_status:#x}, driver {h.driver_status:#x})")
        return buf.raw[:inlen] if inlen else b""

    def inquiry(self):
        d = self.run(bytes([0x12, 0, 0, 0, 36, 0]), inlen=36)
        return d[8:16].decode("ascii", "replace").strip(), d[16:32].decode("ascii", "replace").strip()


def cdb(op, sub, args=b""):
    c = bytes([op, sub]) + args
    return c + b"\xFF" * (16 - len(c))


class Boot:
    """the UBOOT1.00 ROM and, once it runs, the helper loader: the same command set (protocol v2)"""

    def __init__(self, sg):
        self.sg = sg

    def mem_write(self, addr, data):
        self.sg.run(cdb(0xFB, 0x06, addr.to_bytes(4, "big") + len(data).to_bytes(2, "big") + b"\x00"
                        + crc16(data).to_bytes(2, "little")), out=data)

    def mem_read(self, addr, n):
        return self.sg.run(cdb(0xFD, 0x07, addr.to_bytes(4, "big") + n.to_bytes(2, "big")), inlen=n)

    def mem_jump(self, addr, arg):
        return self.sg.run(cdb(0xFB, 0x08, addr.to_bytes(4, "big") + arg.to_bytes(2, "big")), inlen=16)

    def read_id(self):
        return self.sg.run(cdb(0xFC, 0x0B), inlen=16)

    def flash_read(self, addr, n):
        return self.sg.run(cdb(0xFD, 0x05, addr.to_bytes(4, "big") + n.to_bytes(2, "big")), inlen=n)

    def flash_erase_sector(self, addr):
        self.sg.run(cdb(0xFB, 0x01, addr.to_bytes(4, "big")), inlen=16, timeout_ms=30000)

    def flash_write(self, addr, data):
        self.sg.run(cdb(0xFB, 0x04, addr.to_bytes(4, "big") + len(data).to_bytes(2, "big") + b"\x00"
                        + crc16(data).to_bytes(2, "little")), out=data)

    def run_app(self):
        self.sg.run(cdb(0xFC, 0x0C, (1).to_bytes(4, "big")), inlen=16, timeout_ms=3000)

    def read(self, addr, n):
        return b"".join(self.flash_read(addr + o, min(CH, n - o)) for o in range(0, n, CH))


# ---------------------------------------------------------------- package ---

def logical_image(raw):
    """the .fwsc without the 20 identity marker bytes (one after each of the first 20 0x2F-byte blocks)"""
    if len(raw) < 20 * 0x30:
        die("not an FM-1 package (too short)")
    product = "".join(chr((raw[i * 0x30 + 0x2F] - i - 1) & 0xFF) for i in range(20) if raw[i * 0x30 + 0x2F] != 0x7D)
    out = bytearray()
    for i in range(20):
        out += raw[i * 0x30:i * 0x30 + 0x2F]
    return product, bytes(out + raw[20 * 0x30:])


def app_area(raw, name):
    """the package's flash.bin app area [0x4000, 0x93000), after the package's own checks"""
    if LOADER_MARK not in raw:
        die(f"{name}: not a Felucca-format package (no Felucca update loader in it). Most hub firmwares are; "
            "M-VAVE's official V15 is not: rescue to a hub firmware first, then install V15 from the hub page.")
    product, img = logical_image(raw)
    hdr = bytearray(img[:0x40])
    enc(hdr, 0, 0x40)
    count = struct.unpack_from("<H", hdr, 8)[0]
    if crc16(hdr[2:0x40]) != struct.unpack_from("<H", hdr, 0)[0] or not 1 <= count <= 11:
        die(f"{name}: damaged package (header)")
    lst = img[0x40:0x40 + 0x50 * count]
    if crc16(lst) != struct.unpack_from("<H", hdr, 2)[0]:
        die(f"{name}: damaged package (file list)")
    for i in range(count):
        e = bytearray(lst[i * 0x50:(i + 1) * 0x50])
        enc(e, 0, 0x50)
        typ, dcrc = struct.unpack_from("<H", e, 0)[0], struct.unpack_from("<H", e, 4)[0]
        off, size = struct.unpack_from("<II", e, 8)
        if off + size > len(img) or crc16(img[off:off + size]) != dcrc:
            die(f"{name}: damaged package (file {i})")
        if typ == 0:                                     # flash.bin
            if size < APP_HI:
                die(f"{name}: flash.bin is too short ({size} B)")
            return product, img[off + APP_LO:off + APP_HI]
    die(f"{name}: no flash.bin in the package")


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "fm1-rescue (designburgapps.com)"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def package(arg):
    if os.path.isfile(arg):                              # a file; anything else (a folder of that name too) is a hub id
        return arg, open(arg, "rb").read()
    cat = json.loads(fetch(HUB + "/catalog.json"))["firmwares"]
    f = next((x for x in cat if x["id"] == arg), None)
    if not f:
        die(f"{arg}: no such file, and no firmware with that id on the hub. Ids: " + ", ".join(x["id"] for x in cat))
    raw = fetch(f"{HUB}/{f['pkg']}")
    if hashlib.sha256(raw).hexdigest() != f["sha256"]:
        die(f"{f['pkg']}: download does not match the hub's SHA-256, try again")
    return f"{f['name']} {f['version']} (hub)", raw


def helper_loader():
    cache = Path(os.environ.get("SUDO_USER") and f"~{os.environ['SUDO_USER']}" or "~").expanduser() / ".cache" / "fm1-rescue"
    p = cache / "wl82loader.bin"
    data = p.read_bytes() if p.exists() else fetch(JL_URL)
    if hashlib.sha256(data).hexdigest() != JL_SHA256:
        die("the helper loader does not match its SHA-256 (download damaged?): delete " + str(p))
    try:
        cache.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        if os.environ.get("SUDO_UID"):                  # (the user's cache stays the user's)
            for q in (cache, p):
                os.chown(q, int(os.environ["SUDO_UID"]), int(os.environ["SUDO_GID"]))
    except OSError:
        pass
    return data


# ------------------------------------------------------------ the device ---

def find_uboot():
    for d in sorted(glob.glob("/sys/class/scsi_generic/sg*")):
        try:
            model = Path(d, "device/model").read_text().strip()
        except OSError:
            continue
        if model.startswith("UBOOT"):
            return "/dev/" + os.path.basename(d)
    return None


def send_uboot_key():
    """an FM-1 still running a firmware: the bootloader key over USB-MIDI (the direction that still works)"""
    sent = False
    for idf in glob.glob("/proc/asound/card*/id"):
        cid = Path(idf).read_text().strip()
        if not any(k in cid.lower() for k in ("felucca", "fm1", "fm-1", "update")):
            continue
        n = idf.split("card")[1].split("/")[0]
        for dev in glob.glob(f"/dev/snd/midiC{n}D*"):
            try:
                fd = os.open(dev, os.O_WRONLY)
                os.write(fd, UBOOT_KEY)
                os.close(fd)
                print(f"sent the bootloader key to {cid} ({dev})")
                sent = True
            except OSError as e:
                print(f"{dev}: {e.strerror}")
    return sent


def chip_key(b):
    """the chip key from the FM-1's own flash head: JLFS entries (ENC) -> isd_config.ini -> key blob"""
    head = bytearray(b.read(0, 0x400))
    for off in range(32, 0x400, 32):
        e = bytearray(head[off:off + 32])
        enc(e, 0, 32)
        if crc16(e[2:32]) != struct.unpack_from("<H", e, 0)[0]:
            die("the flash head does not read as JieLi's (CRC): stopping, nothing written")
        if bytes(e[16:31]) == b"isd_config.ini\0":
            blob = b.read(struct.unpack_from("<I", e, 4)[0], 34)
            if crc16(blob[:32]) != struct.unpack_from("<H", blob, 32)[0]:
                die("the chip key block is damaged: stopping, nothing written")
            s = sum(blob[:16]) & 0xFF
            s = 0xAA if s >= 0xE0 else 0x55 if s <= 0x10 else s
            return sum(1 << i for i in range(16) if (blob[16 + i] ^ blob[15 - i]) < s)
        if struct.unpack_from("<H", e, 14)[0]:
            break
    die("no chip key in the flash head: stopping, nothing written")


def main():
    ap = argparse.ArgumentParser(description="Put a firmware back on a stuck M-VAVE FM-1 (Linux, as root).")
    ap.add_argument("firmware", help="a hub id (e.g. sloop, felucca, zp12) or a .fwsc file")
    ap.add_argument("--check", action="store_true", help="read and compare only, write nothing")
    ap.add_argument("--yes", action="store_true", help="write without asking")
    ap.add_argument("--device", help="the bootloader's SCSI device (default: found by its name)")
    a = ap.parse_args()
    if not sys.platform.startswith("linux"):
        die("Linux only for now (it needs the SCSI pass-through)")
    if os.geteuid() != 0:
        die("run it with sudo: the bootloader is a USB disk only root may talk to")

    name, raw = package(a.firmware)
    product, new = app_area(raw, name)
    print(f"firmware: {name}, identity {product}")
    jl = helper_loader()

    dev = a.device or find_uboot()
    if not dev:
        if not send_uboot_key():
            die("no FM-1 found: plug it in by USB (or press 'Send to the bootloader' on the hub page) and run again")
        for _ in range(40):
            time.sleep(0.5)
            if (dev := find_uboot()):
                break
        if not dev:
            die("the FM-1 did not show up as UBOOT1.00: press 'Send to the bootloader' on the hub page, then run again")
    for _ in range(20):                                  # (udev may still be setting it up)
        try:
            sg = Sg(dev)
            vendor, model = sg.inquiry()
            break
        except (OSError, ScsiError):
            time.sleep(0.5)
    else:
        die(f"{dev}: cannot open it")
    print(f"bootloader: {vendor} {model} at {dev}")
    if not model.startswith("UBOOT1.00") or vendor != "WL82":
        die(f"{vendor} {model}: not the FM-1's chip (WL82 UBOOT1.00): stopping")

    b = Boot(sg)
    # a run cut short may have left the helper running: never write it over itself. Both answer a memory read; the
    # ROM sends RAM MengLi-ciphered, the helper sends it plain, so the helper's own first bytes, plain, mean it runs
    running = b.mem_read(JL_ADDR, 16) == mengli(jl[:16])
    if running:
        print("the flash helper is already running (an earlier run): going on with it")
    else:
        for off in range(0, len(jl), 512):               # stored MengLi-encrypted; the ROM decrypts it as it lands
            b.mem_write(JL_ADDR + off, jl[off:off + 512])
        b.mem_jump(JL_ADDR, JL_ARG)
    rid = b.read_id()                                    # (the helper answers one byte late: 00 85 60 ...)
    if FLASH_ID[:2] not in rid:
        die(f"the flash chip is not the FM-1's (id answer {rid.hex(' ')}): stopping, nothing written "
            "(power the FM-1 off and on)")
    key = chip_key(b)
    blk = bytearray(new[:32])
    sfc(blk, 32, key)
    if crc16(blk[2:32]) != struct.unpack_from("<H", blk, 0)[0]:
        die(f"this package is not for this FM-1's chip key ({key:#06x}): stopping, nothing written")

    print("reading the firmware there now ...", flush=True)
    todo = [s for s in range(APP_LO, APP_HI, SEC) if b.read(s, SEC) != new[s - APP_LO:s - APP_LO + SEC]]
    print(f"{len(todo)} of {(APP_HI - APP_LO) // SEC} sectors to write")
    if a.check:
        print("--check: nothing written. The FM-1 stays in the bootloader: power it off and on (or run without --check).")
        return
    if todo and not a.yes and input(f"write {name} to the FM-1 now? type yes: ").strip() != "yes":
        print("nothing written. The FM-1 stays in the bootloader: power it off and on to get the old firmware back.")
        return
    for n, s in enumerate(todo, 1):
        want = new[s - APP_LO:s - APP_LO + SEC]
        for _ in range(3):
            b.flash_erase_sector(s)
            for o in range(0, SEC, CH):
                b.flash_write(s + o, want[o:o + CH])
            if b.read(s, SEC) == want:
                break
        else:
            die(f"sector {s:#x} does not read back as written: stopping. Run the rescue again (it resumes).")
        print(f"\r  {n}/{len(todo)} sectors written and checked", end="", flush=True)
    print()
    bad = [s for s in range(APP_LO, APP_HI, SEC) if b.read(s, SEC) != new[s - APP_LO:s - APP_LO + SEC]]
    if bad:
        die("the final check failed at " + ", ".join(hex(x) for x in bad) + ": run the rescue again")
    print(f"done: {name} is on the FM-1, all checked. It restarts now (if not: power it off and on).")
    try:
        b.run_app()
    except (OSError, ScsiError):
        pass
    sg.close()


if __name__ == "__main__":
    main()
