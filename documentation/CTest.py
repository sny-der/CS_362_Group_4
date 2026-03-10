# This will be the pace I paste the python script to test the C file.
# It was translated from linux by ChatGPT.

#!/usr/bin/env python3
"""
test_bridge_windows.py

Windows test harness for the IPv6 UDP bridge (contest_pybridge.exe).

Requirements:
  - Windows 10/11
  - Python 3.8+
  - IPv6 enabled on loopback (default)
  - Compiled bridge executable OR build toolchain:
      * MSVC cl.exe OR MinGW-w64 gcc.exe (optional; can compile manually)

Usage (PowerShell):
  python .\test_bridge_windows.py --exe .\contest_pybridge.exe

Build + run (MSVC Developer Prompt):
  python .\test_bridge_windows.py --build --cfile .\ipv6test_windows.c --exe .\contest_pybridge.exe
"""

import argparse
import hashlib
import os
import queue
import select
import shutil
import socket
import struct
import subprocess
import threading
import time
from dataclasses import dataclass
from typing import Optional, Tuple, List, Set

CTRL_TYPE_LEN = 8
PEER_TYPE_LEN = 12

# Control packet types (8 bytes)
PKT_MSG = b"MSG-----"
PKT_EXIT = b"EXIT----"
PKT_INFO = b"INFO----"
PKT_CTLPORT = b"CTLPORT-"
PKT_MYENDP = b"MYENDP--"
PKT_MKLOCAL = b"MKLOCAL-"
PKT_SETPEER = b"SETPEER-"
PKT_SNDFILE = b"SNDFILE-"
PKT_GETMSG = b"GETMSG--"

# Peer packet types (12 bytes)
PEER_FILECHNK = b"FILECHNK----"
PEER_FILEREQ = b"FILEREQ-----"


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1024 * 1024)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def encode_ctrl(type8: bytes, payload: bytes = b"") -> bytes:
    assert len(type8) == CTRL_TYPE_LEN
    return type8 + payload


def decode_ctrl(dat: bytes):
    if len(dat) < CTRL_TYPE_LEN:
        raise ValueError("ctrl packet too small")
    return dat[:CTRL_TYPE_LEN], dat[CTRL_TYPE_LEN:]


@dataclass
class CtrlEvent:
    typ: bytes
    payload: bytes
    addr: Tuple[str, int, int, int]
    t: float


class Controller:
    """
    Owns one "python receiver" UDP socket and one bridge process instance.
    """

    def __init__(self, name: str, exe: str):
        self.name = name
        self.exe = exe
        self.sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.sock.bind(("::1", 0))
        self.py_port = self.sock.getsockname()[1]
        self.proc: Optional[subprocess.Popen] = None

        self.ctrl_port: Optional[int] = None
        self.ctrl_addr = ("::1", 0)

        self._q: "queue.Queue[CtrlEvent]" = queue.Queue()
        self._stop = threading.Event()
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)

    def start(self):
        self.proc = subprocess.Popen(
            [self.exe, str(self.py_port)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self._rx_thread.start()

        ev = self.wait_for(PKT_CTLPORT, timeout=8.0)
        port_s = ev.payload.decode("utf-8", errors="replace").strip()
        self.ctrl_port = int(port_s)
        self.ctrl_addr = ("::1", self.ctrl_port)
        print(f"[{self.name}] CTLPORT- -> {self.ctrl_port}")

    def stop(self):
        try:
            if self.ctrl_port:
                self.send_cmd(PKT_EXIT, b"")
        except Exception:
            pass

        self._stop.set()
        try:
            self.sock.close()
        except Exception:
            pass

        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
            except Exception:
                pass
            try:
                self.proc.wait(timeout=2.0)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass

    def _rx_loop(self):
        while not self._stop.is_set():
            r, _, _ = select.select([self.sock], [], [], 0.25)
            if not r:
                continue
            try:
                dat, addr = self.sock.recvfrom(CTRL_TYPE_LEN + 8192)
            except OSError:
                break
            try:
                typ, payload = decode_ctrl(dat)
            except Exception:
                continue
            self._q.put(CtrlEvent(typ=typ, payload=payload, addr=addr, t=time.time()))

    def send_cmd(self, typ: bytes, payload: bytes):
        if self.ctrl_port is None:
            raise RuntimeError("ctrl_port not ready")
        self.sock.sendto(encode_ctrl(typ, payload), self.ctrl_addr)

    def wait_for(self, typ: bytes, timeout: float) -> CtrlEvent:
        deadline = time.time() + timeout
        backlog: List[CtrlEvent] = []

        while time.time() < deadline:
            try:
                ev = self._q.get(timeout=0.25)
            except queue.Empty:
                continue

            if ev.typ == PKT_INFO:
                msg = ev.payload.decode("utf-8", errors="replace")
                print(f"[{self.name}] INFO: {msg}")

            if ev.typ == typ:
                for b in backlog:
                    self._q.put(b)
                return ev

            backlog.append(ev)

        for b in backlog:
            self._q.put(b)
        raise TimeoutError(f"[{self.name}] timeout waiting for {typ!r}")


class ProxyRelay:
    """
    UDP IPv6 relay between two bridges' peer sockets.
    Can drop specific FILECHNK chunk indices ONCE (A->B) to force FILEREQ.

    Each peer is configured to send to:
      - C1 -> proxyA port
      - C2 -> proxyB port
    Relay learns the return addresses from traffic and forwards appropriately.
    """

    def __init__(self, drop_once: Set[int]):
        self.sock_a = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.sock_b = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.sock_a.bind(("::1", 0))
        self.sock_b.bind(("::1", 0))
        self.port_a = self.sock_a.getsockname()[1]
        self.port_b = self.sock_b.getsockname()[1]

        self.addr_a_peer: Optional[Tuple[str, int, int, int]] = None
        self.addr_b_peer: Optional[Tuple[str, int, int, int]] = None

        self.drop_once = set(drop_once)
        self.dropped: Set[int] = set()

        self.seen_reqs: List[List[int]] = []
        self._req_lock = threading.Lock()

        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        try:
            self.sock_a.close()
        except Exception:
            pass
        try:
            self.sock_b.close()
        except Exception:
            pass

    def _type12(self, dat: bytes) -> bytes:
        if len(dat) < PEER_TYPE_LEN:
            return b""
        return dat[:PEER_TYPE_LEN]

    def _chunk_index(self, dat: bytes) -> Optional[int]:
        # type12 + transfer_id(16) + chunk_index(4)
        if len(dat) < PEER_TYPE_LEN + 16 + 4:
            return None
        return struct.unpack("!I", dat[PEER_TYPE_LEN + 16: PEER_TYPE_LEN + 20])[0]

    def _parse_filereq_indices(self, dat: bytes) -> Optional[List[int]]:
        base = PEER_TYPE_LEN
        if len(dat) < base + 16 + 2:
            return None
        count = struct.unpack("!H", dat[base + 16: base + 18])[0]
        indices = []
        off = base + 18
        for _ in range(count):
            if off + 4 > len(dat):
                break
            indices.append(struct.unpack("!I", dat[off:off + 4])[0])
            off += 4
        return indices

    def _record_req(self, indices: List[int]):
        with self._req_lock:
            self.seen_reqs.append(indices)

    def wait_for_requested(self, want: Set[int], timeout: float) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._req_lock:
                flat = set(i for batch in self.seen_reqs for i in batch)
            if want.issubset(flat):
                return True
            time.sleep(0.05)
        return False

    def _loop(self):
        while not self._stop.is_set():
            r, _, _ = select.select([self.sock_a, self.sock_b], [], [], 0.2)
            for s in r:
                try:
                    dat, addr = s.recvfrom(4096)
                except OSError:
                    continue

                if s is self.sock_a:
                    self.addr_a_peer = addr
                    if self.addr_b_peer is None:
                        continue
                    t = self._type12(dat)
                    if t == PEER_FILECHNK:
                        idx = self._chunk_index(dat)
                        if idx is not None and idx in self.drop_once and idx not in self.dropped:
                            self.dropped.add(idx)
                            print(f"[PROXY] Dropping FILECHNK idx={idx} once (A->B)")
                            continue
                    self.sock_b.sendto(dat, self.addr_b_peer)

                else:
                    self.addr_b_peer = addr
                    if self.addr_a_peer is None:
                        continue
                    t = self._type12(dat)
                    if t == PEER_FILEREQ:
                        indices = self._parse_filereq_indices(dat)
                        if indices:
                            print(f"[PROXY] Saw FILEREQ (B->A): {indices[:10]}{'...' if len(indices) > 10 else ''}")
                            self._record_req(indices)
                    self.sock_a.sendto(dat, self.addr_a_peer)


def find_tool(name: str) -> Optional[str]:
    return shutil.which(name)


def build_exe_msvc(cfile: str, exe: str):
    # Requires Visual Studio Developer Command Prompt
    cmd = ["cl", "/nologo", "/O2", "/W4", "/D_CRT_SECURE_NO_WARNINGS", cfile, f"/Fe:{exe}", "ws2_32.lib"]
    print("[BUILD MSVC]", " ".join(cmd))
    subprocess.check_call(cmd)


def build_exe_mingw(cfile: str, exe: str):
    cmd = ["gcc", "-O2", "-Wall", "-Wextra", "-o", exe, cfile, "-lws2_32"]
    print("[BUILD MinGW]", " ".join(cmd))
    subprocess.check_call(cmd)


def ensure_exe_built(exe: str):
    if os.path.exists(exe):
        return
    if not exe.lower().endswith(".exe") and os.path.exists(exe + ".exe"):
        return
    raise FileNotFoundError(f"Executable not found: {exe}. Compile it first or run with --build.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfile", default="ipv6test_windows.c")
    ap.add_argument("--exe", default="contest_pybridge.exe")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--toolchain", choices=["auto", "msvc", "mingw"], default="auto")
    ap.add_argument("--drop", default="5,25,50")
    ap.add_argument("--file1-bytes", type=int, default=350_000)
    ap.add_argument("--file2-bytes", type=int, default=220_000)
    args = ap.parse_args()

    drop_once: Set[int] = set()
    if args.drop.strip():
        for part in args.drop.split(","):
            part = part.strip()
            if part:
                drop_once.add(int(part))

    exe = args.exe

    if args.build:
        tc = args.toolchain
        if tc == "auto":
            tc = "msvc" if find_tool("cl") else ("mingw" if find_tool("gcc") else "none")
        if tc == "msvc":
            build_exe_msvc(args.cfile, exe)
        elif tc == "mingw":
            build_exe_mingw(args.cfile, exe)
        else:
            raise RuntimeError(
                "No compiler found. Install MSVC (Developer Prompt) or MinGW-w64 gcc, or compile manually."
            )
    else:
        ensure_exe_built(exe)

    os.makedirs("TestFiles", exist_ok=True)
    f1 = os.path.abspath(os.path.join("TestFiles", "test1.bin"))
    f2 = os.path.abspath(os.path.join("TestFiles", "test2.bin"))
    with open(f1, "wb") as fp:
        fp.write(os.urandom(args.file1_bytes))
    with open(f2, "wb") as fp:
        fp.write(os.urandom(args.file2_bytes))

    h1 = sha256_file(f1)
    h2 = sha256_file(f2)
    print("[TEST] file1:", f1, "sha256", h1)
    print("[TEST] file2:", f2, "sha256", h2)

    c1 = Controller("C1", exe)
    c2 = Controller("C2", exe)
    proxy = ProxyRelay(drop_once=drop_once)

    try:
        c1.start()
        c2.start()

        c1.send_cmd(PKT_MKLOCAL, b"")
        c2.send_cmd(PKT_MKLOCAL, b"")

        e1 = c1.wait_for(PKT_MYENDP, timeout=8.0).payload.decode("utf-8", errors="replace")
        e2 = c2.wait_for(PKT_MYENDP, timeout=8.0).payload.decode("utf-8", errors="replace")
        print("[TEST] C1 endpoint:", e1)
        print("[TEST] C2 endpoint:", e2)

        proxy.start()

        c1.send_cmd(PKT_SETPEER, f"[::1]:{proxy.port_a}".encode("utf-8"))
        c2.send_cmd(PKT_SETPEER, f"[::1]:{proxy.port_b}".encode("utf-8"))

        time.sleep(1.0)

        # Chat test
        msg = b"hello from windows test script"
        c1.send_cmd(PKT_MSG, msg)

        got = None
        t0 = time.time()
        while time.time() - t0 < 6.0:
            c2.send_cmd(PKT_GETMSG, b"")
            try:
                ev = c2.wait_for(PKT_MSG, timeout=0.8)
                got = ev.payload
                break
            except TimeoutError:
                continue

        assert got is not None, "did not receive chat message at C2"
        got_s = got.decode("utf-8", errors="replace")
        print("[TEST] Chat received at C2:", got_s)
        assert got_s == msg.decode(), "chat payload mismatch"

        # File transfer tests
        print("[TEST] Sending file1...")
        c1.send_cmd(PKT_SNDFILE, f1.encode("utf-8"))

        print("[TEST] Queueing file2 while file1 in progress...")
        c1.send_cmd(PKT_SNDFILE, f2.encode("utf-8"))

        received_paths: List[str] = []
        deadline = time.time() + 120.0
        while time.time() < deadline and len(received_paths) < 2:
            try:
                ev = c2.wait_for(PKT_INFO, timeout=2.0)
            except TimeoutError:
                continue
            s = ev.payload.decode("utf-8", errors="replace")
            if s.startswith("File received: "):
                path = s[len("File received: "):].strip()
                received_paths.append(path)
                print("[TEST] Receiver reports:", path)

        assert len(received_paths) >= 2, "did not receive both files within timeout"

        # Retransmit observation
        if drop_once:
            ok = proxy.wait_for_requested(drop_once, timeout=30.0)
            assert ok, f"did not observe FILEREQ requesting all dropped indices: {drop_once}"
            print("[TEST] FILEREQ observed for dropped indices:", sorted(drop_once))

        # Integrity
        hashes = {}
        for p in received_paths:
            if not os.path.exists(p):
                raise AssertionError(f"received path does not exist: {p}")
            hashes[p] = sha256_file(p)
            print("[TEST] received", p, "sha256", hashes[p])

        got1 = any(h == h1 for h in hashes.values())
        got2 = any(h == h2 for h in hashes.values())
        assert got1 and got2, "received files do not match original hashes"

        print("\n[PASS] All tests passed.")

    finally:
        proxy.stop()
        c1.stop()
        c2.stop()


if __name__ == "__main__":
    main()
