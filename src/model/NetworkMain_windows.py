#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import socket
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional
import sys

if getattr(sys, "frozen", False):
    # Running inside PyInstaller bundle
    MODULE_DIR = Path(sys._MEIPASS)
else:
    # Running normally from source
    MODULE_DIR = Path(__file__).resolve().parent

# Windows needs an .exe suffix; POSIX does not.
C_EXE_BASENAME = "contest_pybridge"
C_EXE_PATH = MODULE_DIR / "model" / (C_EXE_BASENAME + (".exe" if os.name == "nt" else ""))

STATE_FILE_PATH = MODULE_DIR / "NetworkBridgeState.txt"

HOST = "::1"
RX_BUFSZ = 8192
TYPE_LEN = 8
STARTUP_TIMEOUT = 5.0
REQUEST_TIMEOUT = 2.0
MESSAGE_LIMIT = 1000

PKT_MSG = b"MSG-----"
PKT_EXIT = b"EXIT----"
PKT_INFO = b"INFO----"
PKT_CTLPORT = b"CTLPORT-"
PKT_MYENDP = b"MYENDP--"
PKT_MKLOCAL = b"MKLOCAL-"
PKT_MKPUB = b"MKPUB---"
PKT_SETPEER = b"SETPEER-"
PKT_SNDFILE = b"SNDFILE-"
PKT_GETMSG = b"GETMSG--"
PKT_GETENDP = b"GETENDP-"


@dataclass
class BridgeRuntime:
    sock: socket.socket
    recv_port: int
    proc: subprocess.Popen
    control_port: Optional[int] = None
    last_endpoint: str = ""
    current_mode: str = ""
    running: bool = True
    control_port_event: threading.Event = field(default_factory=threading.Event)
    endpoint_event: threading.Event = field(default_factory=threading.Event)
    message_response_event: threading.Event = field(default_factory=threading.Event)
    pending_message_response: str = ""
    last_info: str = ""
    lock: threading.Lock = field(default_factory=threading.Lock)


_runtime: Optional[BridgeRuntime] = None


def _print_problem(message: str) -> None:
    print(f"[NetworkMain] {message}")


def _write_state_file() -> None:
    lines = [
        "python_loopback=",
        "control_loopback=",
        "shareable_endpoint=",
        "mode=",
        "pid=",
    ]

    rt = _runtime
    if rt is not None:
        recv_text = f"[{HOST}]:{rt.recv_port}"
        control_text = f"[{HOST}]:{rt.control_port}" if rt.control_port else ""
        lines = [
            f"python_loopback={recv_text}",
            f"control_loopback={control_text}",
            f"shareable_endpoint={rt.last_endpoint}",
            f"mode={rt.current_mode}",
            f"pid={rt.proc.pid if rt.proc.poll() is None else ''}",
        ]

    STATE_FILE_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _clear_state_file() -> None:
    STATE_FILE_PATH.write_text(
        "python_loopback=\ncontrol_loopback=\nshareable_endpoint=\nmode=\npid=\n",
        encoding="utf-8",
    )


def _read_state_file() -> dict[str, str]:
    result: dict[str, str] = {}
    if not STATE_FILE_PATH.exists():
        return result
    try:
        for line in STATE_FILE_PATH.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                result[key.strip()] = value.strip()
    except OSError:
        return {}
    return result


def _exe_is_usable(path: Path) -> bool:
    if not path.exists():
        return False
    if os.name == "nt":
        # Windows doesn't have an executable bit; existence is enough.
        return path.suffix.lower() == ".exe"
    return os.access(path, os.X_OK)




def _ensure_c_executable() -> bool:
    if _exe_is_usable(C_EXE_PATH):
        return True

    _print_problem(f"C bridge executable not found at {C_EXE_PATH}")
    return False


def _build_packet(pkt_type: bytes, payload_text: str = "") -> bytes:
    if len(pkt_type) != TYPE_LEN:
        raise ValueError("packet type must be exactly 8 bytes")
    return pkt_type + payload_text.encode("utf-8", errors="replace")


def _send_packet(dest_port: int, pkt_type: bytes, payload_text: str = "") -> None:
    rt = _runtime
    if rt is None:
        raise RuntimeError("runtime is not initialized")
    rt.sock.sendto(_build_packet(pkt_type, payload_text), (HOST, dest_port))


def _parse_packet(data: bytes) -> tuple[bytes, str]:
    if len(data) < TYPE_LEN:
        raise ValueError("packet too short")
    return data[:TYPE_LEN], data[TYPE_LEN:].decode("utf-8", errors="replace")


# Internal helper requested by the design: startup() uses this to complete the
# two-way Python<->C control link and returns (send_to_control, receive_from_python).
def _establishconnection_internal(rt: BridgeRuntime) -> tuple[str, str]:
    deadline = time.time() + STARTUP_TIMEOUT
    while time.time() < deadline:
        if rt.control_port_event.wait(timeout=0.05):
            break
        if rt.proc.poll() is not None:
            raise RuntimeError("C bridge exited before sending its control port")

    if not rt.control_port_event.is_set() or rt.control_port is None:
        raise RuntimeError("Timed out waiting for the C bridge control port")

    _write_state_file()
    return (f"[{HOST}]:{rt.control_port}", f"[{HOST}]:{rt.recv_port}")


def _receiver_loop(rt: BridgeRuntime) -> None:
    while rt.running:
        try:
            data, _addr = rt.sock.recvfrom(RX_BUFSZ)
        except OSError:
            return

        try:
            pkt_type, payload = _parse_packet(data)
        except ValueError:
            _print_problem("Ignored malformed packet from the C bridge.")
            continue

        if pkt_type == PKT_CTLPORT:
            try:
                rt.control_port = int(payload)
            except ValueError:
                _print_problem("Received an invalid CTLPORT payload.")
                continue
            rt.control_port_event.set()
            _write_state_file()
            continue

        if pkt_type == PKT_MYENDP:
            with rt.lock:
                rt.last_endpoint = payload
            rt.endpoint_event.set()
            _write_state_file()
            continue

        if pkt_type == PKT_INFO:
            rt.last_info = payload
            if payload == "GETMSG:EMPTY":
                rt.pending_message_response = ""
                rt.message_response_event.set()
                continue
            _print_problem(payload)
            if "Closing this bridge" in payload or "remote peer ended" in payload.lower():
                rt.running = False
            continue

        if pkt_type == PKT_MSG:
            rt.pending_message_response = payload
            rt.message_response_event.set()
            continue

        if pkt_type == PKT_EXIT:
            rt.running = False
            _print_problem("The C bridge sent EXIT.")
            continue

        _print_problem(f"Unknown packet type from C: {pkt_type!r}")


def _cleanup_runtime(send_exit: bool) -> None:
    global _runtime
    rt = _runtime
    if rt is None:
        _clear_state_file()
        return

    if send_exit and rt.proc.poll() is None and rt.control_port is not None:
        try:
            _send_packet(rt.control_port, PKT_EXIT, "")
            time.sleep(0.15)
        except Exception:
            pass

    rt.running = False
    try:
        rt.sock.close()
    except OSError:
        pass

    if rt.proc.poll() is None:
        try:
            rt.proc.terminate()
            rt.proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            rt.proc.kill()
            rt.proc.wait(timeout=2)
        except Exception:
            pass

    _runtime = None
    _clear_state_file()


def _require_running() -> bool:
    if not _ensure_c_executable():
        return False

    rt = _runtime
    if rt is None:
        state = _read_state_file()
        if state.get("control_loopback"):
            _print_problem(
                "A saved state file exists, but this Python process has no active bridge socket. "
                "Call startup() in this process first."
            )
        else:
            _print_problem("The C bridge is not running. Call startup() first.")
        return False

    if rt.proc.poll() is not None or not rt.running:
        _print_problem("The C bridge is no longer running. Call startup() again.")
        return False

    if rt.control_port is None:
        _print_problem("The C bridge control port is not ready yet.")
        return False

    return True


def _request_endpoint(mode_packet: bytes, mode_name: str) -> str:
    if not _require_running():
        return "ERROR: bridge not running"

    rt = _runtime
    assert rt is not None and rt.control_port is not None

    if rt.current_mode == mode_name and rt.last_endpoint:
        return rt.last_endpoint

    rt.endpoint_event.clear()
    _send_packet(rt.control_port, mode_packet, "")
    if not rt.endpoint_event.wait(timeout=REQUEST_TIMEOUT):
        _print_problem("Timed out waiting for the shareable endpoint from the C bridge.")
        return "ERROR: timed out waiting for endpoint"

    rt.current_mode = mode_name
    _write_state_file()
    return rt.last_endpoint


def startup() -> Optional[tuple[str, str]]:
    global _runtime

    if not _ensure_c_executable():
        return None

    if _runtime is not None:
        _cleanup_runtime(send_exit=True)

    _clear_state_file()

    if not socket.has_ipv6:
        _print_problem("Python reports IPv6 is not available on this system.")
        return None

    py_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    py_sock.bind((HOST, 0))
    recv_port = py_sock.getsockname()[1]

    proc = subprocess.Popen([str(C_EXE_PATH), str(recv_port)], cwd=str(MODULE_DIR))
    rt = BridgeRuntime(sock=py_sock, recv_port=recv_port, proc=proc)
    _runtime = rt

    threading.Thread(target=_receiver_loop, args=(rt,), daemon=True).start()

    try:
        return _establishconnection_internal(rt)
    except Exception as exc:
        _print_problem(str(exc))
        _cleanup_runtime(send_exit=False)
        return None


def prepare_public_endpoint() -> str:
    return _request_endpoint(PKT_MKPUB, "public")


def prepare_local_endpoint() -> str:
    return _request_endpoint(PKT_MKLOCAL, "local")


def establish_connection(remote_endpoint: str) -> Optional[str]:
    if not _require_running():
        return None

    local_endpoint = prepare_public_endpoint()
    if local_endpoint.startswith("ERROR:"):
        return None

    rt = _runtime
    assert rt is not None and rt.control_port is not None
    _send_packet(rt.control_port, PKT_SETPEER, remote_endpoint.strip())
    return local_endpoint


def establish_local_connection(other_port_or_endpoint: str | int) -> Optional[str]:
    if not _require_running():
        return None

    local_endpoint = prepare_local_endpoint()
    if local_endpoint.startswith("ERROR:"):
        return None

    endpoint_text = str(other_port_or_endpoint).strip()
    if not endpoint_text:
        _print_problem("A local port or [::1]:port endpoint is required.")
        return None
    if not endpoint_text.startswith("["):
        endpoint_text = f"[{HOST}]:{endpoint_text}"

    rt = _runtime
    assert rt is not None and rt.control_port is not None
    _send_packet(rt.control_port, PKT_SETPEER, endpoint_text)
    return local_endpoint


def get_ipv6_address_port() -> str:
    if not _require_running():
        return "ERROR: bridge not running"

    rt = _runtime
    assert rt is not None and rt.control_port is not None

    if rt.last_endpoint:
        return rt.last_endpoint

    rt.endpoint_event.clear()
    _send_packet(rt.control_port, PKT_GETENDP, "")
    if not rt.endpoint_event.wait(timeout=REQUEST_TIMEOUT):
        _print_problem("Timed out waiting for the endpoint reply.")
        return "ERROR: timed out waiting for endpoint"
    return rt.last_endpoint


def send_message(message: str) -> None:
    if not _require_running():
        return

    rt = _runtime
    assert rt is not None and rt.control_port is not None
    safe_message = (message or "")[:MESSAGE_LIMIT]
    _send_packet(rt.control_port, PKT_MSG, safe_message)


def send_file(file_path: str) -> None:
    if not _require_running():
        return

    path = Path(file_path).expanduser().resolve()
    if not path.exists() or not path.is_file():
        _print_problem(f"File does not exist or is not a regular file: {path}")
        return

    rt = _runtime
    assert rt is not None and rt.control_port is not None
    _send_packet(rt.control_port, PKT_SNDFILE, str(path))


def read_inc_message_queue() -> str:
    if not _require_running():
        return "ERROR: bridge not running"

    rt = _runtime
    assert rt is not None and rt.control_port is not None

    rt.message_response_event.clear()
    rt.pending_message_response = ""
    _send_packet(rt.control_port, PKT_GETMSG, "")
    if not rt.message_response_event.wait(timeout=REQUEST_TIMEOUT):
        _print_problem("Timed out waiting for the latest incoming message.")
        return "ERROR: timed out waiting for latest message"
    return rt.pending_message_response


def terminate_program() -> None:
    _cleanup_runtime(send_exit=True)
