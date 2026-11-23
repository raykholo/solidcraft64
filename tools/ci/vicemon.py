"""vicemon.py - drive VICE (x64sc) through its binary monitor, for the smoke test.

Start x64sc with `-binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502`, then:

    m = Vice()
    m.wait_text("READY.")
    m.type('LOAD"SOLIDCRAFT",8,1\\r')
    m.screenshot("build/shots/01-basic.png")

Our own code, written from VICE's documented protocol (doc/vice.texi, "Binary
Monitor"). Standard library only.
"""
import socket
import struct
import time
import zlib

STX = 0x02
API = 0x02

CMD_MEM_GET = 0x01
CMD_KEYBOARD_FEED = 0x72
CMD_DISPLAY_GET = 0x84
CMD_PALETTE_GET = 0x91
CMD_EXIT = 0xAA
CMD_QUIT = 0xBB

RESPONSE_STOPPED = 0x62
RESPONSE_RESUMED = 0x63


def screen_codes(text):
    """ASCII text as the C64 shows it on the upper-case screen (screen codes)."""
    out = bytearray()
    for ch in text.upper():
        c = ord(ch)
        if 0x40 <= c <= 0x5F:
            out.append(c - 0x40)
        elif 0x20 <= c <= 0x3F:
            out.append(c)
        else:
            raise ValueError("no screen code for %r" % ch)
    return bytes(out)


def petscii(text):
    """Text for the keyboard buffer: upper case, \\r is RETURN, {F1} etc. are keys."""
    keys = {"{F1}": 133, "{F3}": 134, "{F5}": 135, "{F7}": 136, "{F2}": 137, "{F4}": 138,
            "{F6}": 139, "{F8}": 140, "{STOP}": 3, "{DOWN}": 17, "{UP}": 145, "{LEFT}": 157,
            "{RIGHT}": 29, "{HOME}": 19, "{CLR}": 147, "{DEL}": 20}
    out = bytearray()
    i = 0
    while i < len(text):
        if text[i] == "{":
            j = text.index("}", i)
            out.append(keys[text[i:j + 1]])
            i = j + 1
            continue
        c = ord(text[i].upper())
        out.append(13 if text[i] in "\r\n" else c)
        i += 1
    return bytes(out)


def write_png(path, width, height, rows):
    """rows: list of bytes objects, RGB, one per row."""
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


class Vice:
    def __init__(self, host="127.0.0.1", port=6502, timeout=60):
        deadline = time.time() + timeout
        while True:
            try:
                self.s = socket.create_connection((host, port), timeout=10)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)
        self.s.settimeout(30)
        self.req = 0
        self.palette = None

    # -- protocol -------------------------------------------------------
    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            part = self.s.recv(n - len(buf))
            if not part:
                raise ConnectionError("VICE closed the monitor connection")
            buf += part
        return buf

    def _response(self):
        head = self._recv(12)
        stx, api, length, rtype, err, rid = struct.unpack("<BBIBBI", head)
        if stx != STX:
            raise ConnectionError("bad response header")
        return rtype, err, rid, self._recv(length)

    def call(self, cmd, body=b""):
        """Send a command, and return its response's body (skipping events)."""
        self.req += 1
        rid = self.req
        self.s.sendall(struct.pack("<BBIIB", STX, API, len(body), rid, cmd) + body)
        while True:
            rtype, err, got, data = self._response()
            if got == rid and rtype == cmd:
                if err:
                    raise RuntimeError("VICE monitor command 0x%02x failed: error 0x%02x" % (cmd, err))
                return data

    def resume(self):
        """Commands stop the machine; this lets it run again."""
        self.call(CMD_EXIT)

    # -- what the smoke test uses ----------------------------------------
    def mem(self, start, end):
        body = struct.pack("<BHHBH", 0, start, end, 0, 0)
        data = self.call(CMD_MEM_GET, body)
        (n,) = struct.unpack_from("<H", data)
        return data[2:2 + n]

    def screen(self, base=0x0400):
        """The 40x25 text screen as a list of 25 strings (screen codes as text)."""
        raw = self.mem(base, base + 999)
        self.resume()
        lines = []
        for row in range(25):
            line = ""
            for c in raw[row * 40:(row + 1) * 40]:
                c &= 0x7F  # reverse video is the same letter
                if c < 0x20:
                    line += chr(c + 0x40)
                elif c < 0x40:
                    line += chr(c)
                else:
                    line += " "
            lines.append(line)
        return lines

    def text_on_screen(self, text, base=0x0400, below=None):
        """Is text on the screen? With below, only on a line under the last line
        that has `below` on it (for the READY. after a LOADING, say)."""
        lines = self.screen(base)
        start = 0
        if below:
            hits = [i for i, line in enumerate(lines) if below.upper() in line]
            if not hits:
                return False
            start = hits[-1] + 1
        return any(text.upper() in line for line in lines[start:])

    def wait_text(self, text, timeout=120, base=0x0400, below=None):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.text_on_screen(text, base, below):
                return True
            time.sleep(0.5)
        raise TimeoutError("%r never appeared on the screen. It showed:\n%s"
                           % (text, "\n".join(self.screen(base))))

    def type(self, text):
        data = petscii(text)
        # The KERNAL's keyboard buffer holds ten keys: feed in pieces.
        for i in range(0, len(data), 8):
            piece = data[i:i + 8]
            self.call(CMD_KEYBOARD_FEED, bytes([len(piece)]) + piece)
            self.resume()
            time.sleep(0.3)

    def _palette(self):
        data = self.call(CMD_PALETTE_GET, b"\x01")
        (n,) = struct.unpack_from("<H", data)
        pal, off = [], 2
        for _ in range(n):
            size = data[off]
            pal.append(bytes(data[off + 1:off + 4]))
            off += 1 + size
        return pal

    def screenshot(self, path):
        if self.palette is None:
            self.palette = self._palette()
        data = self.call(CMD_DISPLAY_GET, b"\x01\x00")  # VIC-II, 8 bits per pixel
        (fields,) = struct.unpack_from("<I", data)
        dw, dh, xo, yo, iw, ih, bpp = struct.unpack_from("<HHHHHHB", data, 4)
        (blen,) = struct.unpack_from("<I", data, 4 + fields)
        pix = data[8 + fields:8 + fields + blen]
        self.resume()
        # Crop to the visible screen plus a border, as a real TV showed it.
        bx, by = 32, 35
        x0, y0 = max(0, xo - bx), max(0, yo - by)
        x1, y1 = min(dw, xo + iw + bx), min(dh, yo + ih + by)
        pal = self.palette
        rows = []
        for y in range(y0, y1):
            line = pix[y * dw + x0:y * dw + x1]
            rows.append(b"".join(pal[p] if p < len(pal) else b"\0\0\0" for p in line))
        write_png(path, x1 - x0, y1 - y0, rows)

    def quit(self):
        try:
            self.call(CMD_QUIT)
        except (ConnectionError, OSError):
            pass
