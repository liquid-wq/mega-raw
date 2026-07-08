"""
ed_serial.py — Direkte serielle Verbindung zum Mega EverDrive (CORE/PRO)
Implementiert das edlink-Protokoll (aus edlink.exe v1.0.0.8 extrahiert):

  Verbindung:  921600 Baud, nach Open 66 Null-Bytes + Input-Flush,
               Status-Handshake: 2B D4 10 EF -> 2-4 Bytes Antwort (0x5A/0xA5)
  memrd (V1):  2B D4 19 E6 | addr (32-bit LE) | len (32-bit LE) | 00
               -> len Bytes Daten

Vorteil gegenueber edlink.exe-Subprocess: Port bleibt offen,
ein Read dauert wenige ms statt ~270ms.

(c) 2026 Liqui — privates, nicht-kommerzielles Projekt. Nutzung nur unter
Nennung des Urhebers, keine Veroeffentlichung unter fremdem Namen.
"""
import time
import threading

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

CMD_STATUS = 0x10
CMD_MEM_RD = 0x19
CMD_MEM_WR = 0x1A
ADDR_BRAM = 0x01080000
ADDR_MBX  = 0x01800300  # gp mailbox (ADDR_FCI_MBX, 16-bit)


def _cmd(code, sub=None):
    pkt = bytes([0x2B, 0xD4, code, code ^ 0xFF])
    if sub is not None:
        pkt += bytes([sub])
    return pkt


class EdSerial:
    def __init__(self, port="COM5"):
        if serial is None:
            raise RuntimeError("pyserial fehlt: pip install pyserial")
        self.portname = port
        self.ser = None
        self._lock = threading.RLock()

    def open(self):
        self.ser = serial.Serial(self.portname, baudrate=921600, timeout=0.5,
                                 write_timeout=0.5)
        # Wie edlink: 66 Null-Bytes senden, Eingang leeren
        self.ser.write(bytes(66))
        self.ser.flush()
        # Eventuellen Reststrom aktiv leersaugen bis 0.3s Funkstille
        self.ser.timeout = 0.05
        quiet = 0.0
        t_start = time.time()
        while quiet < 0.3 and time.time() - t_start < 3.0:
            chunk = self.ser.read(4096)
            if chunk:
                quiet = 0.0
            else:
                quiet += 0.05
        self.ser.reset_input_buffer()
        self.ser.timeout = 0.5
        # Status-Handshake
        self.ser.write(_cmd(CMD_STATUS))
        self.ser.flush()
        time.sleep(0.05)
        resp = self.ser.read(4)
        if not resp or (0x5A not in resp and 0xA5 not in resp):
            self.close()
            raise RuntimeError(f"EverDrive antwortet nicht (Status: {resp.hex() if resp else 'leer'})")
        self.ser.timeout = 2.0
        return True

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None

    def _recover(self):
        """Toter Handle (USB-Re-Enumeration nach Konsolen-Neustart): Port neu
        suchen — die COM-Nummer kann dabei wechseln — und 1x wiederholen."""
        try: self.close()
        except Exception: pass
        time.sleep(0.8)
        try:
            self.open(); return True
        except Exception:
            pass
        for p in (list_ports.comports() if list_ports else []):
            try:
                self.portname = p.device
                self.open(); return True
            except Exception:
                try: self.close()
                except Exception: pass
        return False

    def memrd(self, addr, length):
        try:
            return self._memrd_raw(addr, length)
        except Exception:
            if self._recover():
                return self._memrd_raw(addr, length)
            raise

    def memwr(self, addr, data):
        try:
            return self._memwr_raw(addr, data)
        except Exception:
            if self._recover():
                return self._memwr_raw(addr, data)
            raise

    def _memrd_raw(self, addr, length):
        """Liest length Bytes vom FCI-Bus (z.B. BRAM bei 0x01080000).
        Hinweis: DEV_MEGA nutzt SwapEndians=true -> 32-bit-Werte BIG-endian."""
        with self._lock:
            self.ser.reset_input_buffer()
            pkt = _cmd(CMD_MEM_RD)
            pkt += addr.to_bytes(4, "big")
            pkt += length.to_bytes(4, "big")
            pkt += b"\x00"
            self.ser.write(pkt)
            self.ser.flush()
            data = self.ser.read(length)
            if len(data) != length:
                raise RuntimeError(f"memrd: {len(data)}/{length} Bytes erhalten")
            return data

    def _memwr_raw(self, addr, data):
        """Schreibt Bytes auf den FCI-Bus (Adresse/Laenge big-endian)."""
        with self._lock:
            pkt = _cmd(CMD_MEM_WR)
            pkt += addr.to_bytes(4, "big")
            pkt += len(data).to_bytes(4, "big")
            pkt += b"\x00"
            pkt += bytes(data)
            self.ser.write(pkt)
            self.ser.flush()

    def wait_bram_frame(self, timeout=0.2):
        """Frame-Handshake: Marker 0xAA nach BRAM[199] schreiben; der Stub
        ueberschreibt das Byte am Ende jedes VBlank-Laufs mit 0x00.
        True = Frame liegt frisch und vollstaendig im BRAM (~14 ms Ruhe
        bis zum naechsten VBlank-Write)."""
        self.memwr(ADDR_BRAM + 198, b"\x00\xAA")
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.memrd(ADDR_BRAM + 198, 2)[1] == 0x00:
                return True
        return False

    def read_bram(self, length=200):
        return self.memrd(ADDR_BRAM, length)

def find_everdrive(preferred=None):
    """Probiert den bevorzugten Port, dann alle anderen. Liefert eine
    geoeffnete EdSerial-Instanz und den Portnamen, sonst (None, None)."""
    candidates = []
    if preferred:
        candidates.append(preferred)
    for p in (list_ports.comports() if list_ports else []):
        if p.device not in candidates:
            candidates.append(p.device)
    for port in candidates:
        try:
            ed = EdSerial(port)
            ed.open()
            return ed, port
        except Exception:
            try: ed.close()
            except Exception: pass
    return None, None

if __name__ == "__main__":
    import sys
    port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
    print(f"Verbinde mit {port} @ 921600...")
    ed = EdSerial(port)
    ed.open()
    print("Verbunden. Erster BRAM-Read:")
    data = ed.read_bram(64)
    for row in range(0, 64, 16):
        print("  " + " ".join(f"{b:02X}" for b in data[row:row+16]))
    print("\nMBX-Diagnose (Register 0x01800300):")
    print(f"  Rohwert jetzt:        {ed.memrd(ADDR_MBX,2).hex()}")
    ed.memwr(ADDR_MBX, b"\x00\x00")
    print(f"  direkt nach Loeschen: {ed.memrd(ADDR_MBX,2).hex()}")
    print("  Verlauf ueber 100 ms (sollte ~16 ms auf 0000 bleiben, dann vom Stub gesetzt werden):")
    ed.memwr(ADDR_MBX, b"\x00\x00")
    t0 = time.time()
    last = None
    while time.time() - t0 < 0.1:
        v = ed.memrd(ADDR_MBX, 2).hex()
        if v != last:
            print(f"    t={ (time.time()-t0)*1000:5.1f} ms  MBX={v}")
            last = v
    print("\nBenchmark: 50 Reads à 200 Bytes...")
    t0 = time.time()
    for _ in range(50):
        ed.read_bram(200)
    dt = time.time() - t0
    print(f"{dt*1000:.0f} ms gesamt = {dt*20:.1f} ms/Read = {50/dt:.0f} Reads/Sekunde")
    ed.close()
