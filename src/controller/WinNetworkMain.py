#!/usr/bin/env python3
"""
NetworkMain_windows.py

Windows 10/11 friendly version of the Python controller. Functionality is the
same as the current Linux script:
  - Starts the C bridge as a subprocess.
  - Uses one local IPv6 UDP socket (::1) for Python <-> C control traffic.
  - Supports local mode and public mode.
  - Supports normal chat messages, 'exit', and '/quit'.
  - Supports file sending when the user types 'file'.
  - Reassembles incoming files into a folder named 'received files'.

Changes for Windows compatibility only:
  - Default executable path resolves to contest_pybridge.exe on Windows.
  - File picker prefers tkinter (works on Windows and Linux) and falls back to
    manual path input if tkinter is unavailable.
  - Windows and Linux both remain supported from the same script.

No packet protocol changes were made.
"""

from __future__ import annotations
from gui import messaging_app

import base64
import os
import socket
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path

HOST = "::1"
RX_BUFSZ = 8192
TYPE_LEN = 8
FILE_CHUNK_SIZE = 700
RECEIVED_DIR_NAME = "received files"

PKT_MSG = b"MSG-----"
PKT_EXIT = b"EXIT----"
PKT_INFO = b"INFO----"
PKT_CTLPORT = b"CTLPORT-"
PKT_MYENDP = b"MYENDP--"
PKT_MKLOCAL = b"MKLOCAL-"
PKT_MKPUB = b"MKPUB---"
PKT_SETPEER = b"SETPEER-"

FILE_META_TAG = "__PYFILEM__"
FILE_CHUNK_TAG = "__PYFILEC__"


def default_c_exe_path() -> str:
    """
    Pick a sensible default executable name for the current OS.
    Windows users will normally compile the C bridge to contest_pybridge.exe.
    Linux users will normally use ./contest_pybridge.
    """
    base_dir = Path(__file__).resolve().parent
    if os.name == "nt":
        exe = base_dir / "contest_pybridge.exe"
        if exe.exists():
            return str(exe)
        # Fallback: allow the same base name even if PATHEXT resolves it.
        return str(base_dir / "contest_pybridge.exe")
    return str(base_dir / "contest_pybridge")


C_EXE_PATH = default_c_exe_path()


@dataclass
class IncomingFile:
    filename: str
    filesize: int
    total_chunks: int
    chunks: dict[int, bytes] = field(default_factory=dict)


@dataclass
class SharedState:
    control_port: int | None = None
    last_endpoint: str | None = None
    control_port_event: threading.Event = field(default_factory=threading.Event)
    endpoint_event: threading.Event = field(default_factory=threading.Event)
    process_should_end: bool = False
    incoming_files: dict[str, IncomingFile] = field(default_factory=dict)
    lock: threading.Lock = field(default_factory=threading.Lock)


def build_packet(pkt_type: bytes, payload_text: str = "") -> bytes:
    if len(pkt_type) != TYPE_LEN:
        raise ValueError("packet type must be exactly 8 bytes")
    return pkt_type + payload_text.encode("utf-8", errors="strict")


def send_packet(sock: socket.socket, dest_port: int, pkt_type: bytes, payload_text: str = "") -> None:
    sock.sendto(build_packet(pkt_type, payload_text), (HOST, dest_port))


def parse_packet(data: bytes) -> tuple[bytes, str]:
    if len(data) < TYPE_LEN:
        raise ValueError("packet too short")
    return data[:TYPE_LEN], data[TYPE_LEN:].decode("utf-8", errors="replace")


def pick_file_with_tk() -> str | None:
    """
    Use tkinter for a native-ish file picker.
    This works well on Windows 10/11 and still works on Linux if tkinter is installed.
    """
    try:
        import tkinter as tk
        from tkinter import filedialog

        root = tk.Tk()
        root.withdraw()
        root.update_idletasks()
        try:
            root.attributes("-topmost", True)
        except Exception:
            pass
        path = filedialog.askopenfilename(title="Select a file to send")
        root.destroy()
        return path or None
    except Exception:
        return None


def open_file_picker() -> str | None:
    print("[python] Opening file selector...")

    path = pick_file_with_tk()
    if path:
        return path

    print("[python] No graphical file picker available. Type a file path instead.")
    manual = input("File path: ").strip()
    return manual or None


def split_file(filepath: str, chunk_size: int = FILE_CHUNK_SIZE) -> dict:
    chunks: list[dict] = []
    file_size = os.path.getsize(filepath)

    with open(filepath, "rb") as f:
        chunk_index = 0
        while True:
            data = f.read(chunk_size)
            if not data:
                break
            chunks.append({"index": chunk_index, "data": data})
            chunk_index += 1

    return {
        "filename": os.path.basename(filepath),
        "filesize": file_size,
        "total_chunks": len(chunks),
        "chunks": chunks,
    }


def get_received_dir() -> Path:
    out_dir = Path(__file__).resolve().parent / RECEIVED_DIR_NAME
    out_dir.mkdir(parents=True, exist_ok=True)
    return out_dir


def unique_output_path(directory: Path, filename: str) -> Path:
    candidate = directory / filename
    if not candidate.exists():
        return candidate

    stem = candidate.stem
    suffix = candidate.suffix
    i = 1
    while True:
        alt = directory / f"{stem} ({i}){suffix}"
        if not alt.exists():
            return alt
        i += 1


def reassemble_file(file_info: dict, output_path: str) -> None:
    chunks = sorted(file_info["chunks"], key=lambda x: x["index"])
    with open(output_path, "wb") as f:
        for chunk in chunks:
            f.write(chunk["data"])


def encode_file_meta(transfer_id: str, filename: str, filesize: int, total_chunks: int) -> str:
    name_b64 = base64.urlsafe_b64encode(filename.encode("utf-8")).decode("ascii")
    return f"{FILE_META_TAG}|{transfer_id}|{name_b64}|{filesize}|{total_chunks}"


def encode_file_chunk(transfer_id: str, index: int, data: bytes) -> str:
    chunk_b64 = base64.b64encode(data).decode("ascii")
    return f"{FILE_CHUNK_TAG}|{transfer_id}|{index}|{chunk_b64}"


def decode_file_meta(payload: str) -> tuple[str, str, int, int]:
    parts = payload.split("|", 4)
    if len(parts) != 5:
        raise ValueError("bad file metadata packet")
    _, transfer_id, name_b64, size_text, total_text = parts
    filename = base64.urlsafe_b64decode(name_b64.encode("ascii")).decode("utf-8", errors="replace")
    return transfer_id, filename, int(size_text), int(total_text)


def decode_file_chunk(payload: str) -> tuple[str, int, bytes]:
    parts = payload.split("|", 3)
    if len(parts) != 4:
        raise ValueError("bad file chunk packet")
    _, transfer_id, index_text, chunk_b64 = parts
    return transfer_id, int(index_text), base64.b64decode(chunk_b64.encode("ascii"))


def is_hidden_file_message(payload: str) -> bool:
    return payload.startswith(FILE_META_TAG) or payload.startswith(FILE_CHUNK_TAG)


def send_file_over_existing_link(sock: socket.socket, control_port: int, filepath: str) -> None:
    file_info = split_file(filepath, FILE_CHUNK_SIZE)
    transfer_id = uuid.uuid4().hex

    print(
        f"[python] Sending file '{file_info['filename']}' "
        f"({file_info['filesize']} bytes in {file_info['total_chunks']} chunks)..."
    )

    send_packet(
        sock,
        control_port,
        PKT_MSG,
        encode_file_meta(
            transfer_id=transfer_id,
            filename=file_info["filename"],
            filesize=file_info["filesize"],
            total_chunks=file_info["total_chunks"],
        ),
    )

    for i, chunk in enumerate(file_info["chunks"], start=1):
        send_packet(sock, control_port, PKT_MSG, encode_file_chunk(transfer_id, chunk["index"], chunk["data"]))
        if i % 25 == 0 or i == file_info["total_chunks"]:
            print(f"[python] Sent chunk {i}/{file_info['total_chunks']}")
        time.sleep(0.003)

    print("[python] File transfer packets queued to the C bridge.")


def finish_incoming_file(state: SharedState, transfer_id: str) -> None:
    incoming = state.incoming_files.get(transfer_id)
    if incoming is None or len(incoming.chunks) != incoming.total_chunks:
        return

    reassembly_info = {
        "filename": incoming.filename,
        "filesize": incoming.filesize,
        "chunks": [{"index": idx, "data": incoming.chunks[idx]} for idx in sorted(incoming.chunks)],
    }

    out_dir = get_received_dir()
    out_path = unique_output_path(out_dir, incoming.filename)
    reassemble_file(reassembly_info, str(out_path))
    actual_size = out_path.stat().st_size
    del state.incoming_files[transfer_id]

    warning = ""
    if actual_size != incoming.filesize:
        warning = f" (warning: expected {incoming.filesize} bytes, wrote {actual_size} bytes)"

    print(f"\n[info] Received file saved to: {out_path}{warning}\npy> ", end="", flush=True)


def handle_hidden_file_message(payload: str, state: SharedState) -> bool:
    if payload.startswith(FILE_META_TAG):
        try:
            transfer_id, filename, filesize, total_chunks = decode_file_meta(payload)
        except Exception:
            print("\n[python] Ignored malformed incoming file metadata.\npy> ", end="", flush=True)
            return True

        with state.lock:
            state.incoming_files[transfer_id] = IncomingFile(filename, filesize, total_chunks)

        print(
            f"\n[info] Incoming file: '{filename}' ({filesize} bytes, {total_chunks} chunks).\npy> ",
            end="",
            flush=True,
        )
        return True

    if payload.startswith(FILE_CHUNK_TAG):
        try:
            transfer_id, index, data = decode_file_chunk(payload)
        except Exception:
            print("\n[python] Ignored malformed incoming file chunk.\npy> ", end="", flush=True)
            return True

        with state.lock:
            incoming = state.incoming_files.get(transfer_id)
            if incoming is None:
                return True
            incoming.chunks[index] = data
            complete = len(incoming.chunks) == incoming.total_chunks

        if complete:
            with state.lock:
                finish_incoming_file(state, transfer_id)
        return True

    return False


def receiver_thread(sock: socket.socket, state: SharedState, view) -> None:
    while True:
        try:
            data, addr = sock.recvfrom(RX_BUFSZ)
        except OSError:
            return

        try:
            pkt_type, payload = parse_packet(data)
        except ValueError:
            print("\n[python] Ignored malformed packet from C.\npy> ", end="", flush=True)
            continue

        if pkt_type == PKT_CTLPORT:
            try:
                state.control_port = int(payload)
                state.control_port_event.set()
                print(f"\n[python] C control port ready: {state.control_port}\npy> ", end="", flush=True)
            except ValueError:
                print("\n[python] Received invalid CTLPORT payload.\npy> ", end="", flush=True)
            continue

        if pkt_type == PKT_MYENDP:
            state.last_endpoint = payload
            state.endpoint_event.set()
            print(f"\n[python] Your shareable endpoint: {payload}\npy> ", end="", flush=True)
            continue

        if pkt_type == PKT_INFO:
            print(f"\n[info] {payload}\npy> ", end="", flush=True)
            view.after(0, lambda: view.display_message("Peer", payload))
            if "Closing this bridge" in payload:
                state.process_should_end = True
            continue

        if pkt_type == PKT_MSG:
            if is_hidden_file_message(payload) and handle_hidden_file_message(payload, state):
                continue
            print(f"\npeer> {payload}\npy> ", end="", flush=True)
            view.after(0, lambda: view.display_message("Peer", payload))
            continue

        if pkt_type == PKT_EXIT:
            print("\n[info] The bridge sent an EXIT packet.\npy> ", end="", flush=True)
            state.process_should_end = True
            continue

        print(f"\n[python] Unknown packet type {pkt_type!r} with payload {payload!r}\npy> ", end="", flush=True)


def wait_for_control_port(state: SharedState, proc: subprocess.Popen) -> int:
    while not state.control_port_event.is_set():
        if proc.poll() is not None:
            raise RuntimeError("C bridge exited before sending its control port")
        time.sleep(0.01)
    assert state.control_port is not None
    return state.control_port

def request_mode_and_endpoint(sock: socket.socket, control_port: int, state: SharedState, view) -> None:
    while True:
        # 1. Ask for mode via GUI instead of terminal
        mode_input = view.get_connection_mode()
        if mode_input is None:
            print("[python] User cancelled mode selection.")
            view.after(0, cancel_connection(sock, control_port, state, view)) # Reset the main "Cancel" button back to "Connect"
            return

        mode = mode_input.strip().lower()

        if mode in ("local", "l"):
            state.endpoint_event.clear()
            send_packet(sock, control_port, PKT_MKLOCAL, "")
            state.endpoint_event.wait()

            view.after(0, lambda: view.update_ip_display(state.last_endpoint))

            other_port = view.get_peer_address("Enter the OTHER terminal's port:", state.last_endpoint)
            
            if other_port:
                send_packet(sock, control_port, PKT_SETPEER, f"[::1]:{other_port}")
                view.after(0, lambda: view.show_status("Local peer configured! Start typing."))
                view.after(0, view.show_chat_ui)
                view.after(0, lambda: view.show_status("Connected to peer!"))
            return
        
        if mode in ("public", "p"):
            state.endpoint_event.clear()
            send_packet(sock, control_port, PKT_MKPUB, "")
            state.endpoint_event.wait()

            view.after(0, lambda: view.update_ip_display(state.last_endpoint))

            remote_endpoint = view.get_peer_address("Enter the OTHER device's [ipv6]:port:", state.last_endpoint)
            if remote_endpoint:
                send_packet(sock, control_port, PKT_SETPEER, remote_endpoint.strip())
                view.after(0, lambda: view.show_status("Public peer configured! Start typing."))
                view.after(0, view.show_chat_ui)
                view.after(0, lambda: view.show_status("Connected to peer!"))
                return
            
def send_gui_message(sock, control_port, view):
    message = view.message_entry.get().strip()
    if message:
        # 1. Send to the C bridge
        send_packet(sock, control_port, PKT_MSG, message)
        # 2. Display on our own screen
        view.display_message("You", message)
        # 3. Clear the input box
        view.message_entry.delete(0, "end")

def launch_connection(sock: socket.socket, control_port: int, state: SharedState, view):
            # 1. Change button to "Cancel" mode
            view.connect_btn.configure(
            text="Cancel Connection", 
            fg_color="red", 
            command=lambda: cancel_connection(sock, control_port, state, view)  #defined below
            )
            # 2. Using a thread prevents the "Wait" from freezing GUI window and starts the connection
            threading.Thread(
                target=request_mode_and_endpoint, 
                args=(sock, control_port, state, view), 
                daemon=True
            ).start()

def cancel_connection(sock: socket.socket, control_port: int, state: SharedState, view):
            # 1. Tell background logic to stop (using SharedState)
            state.process_should_end = True 
            
            # 2. Reset the button back to "Connect" mode
            view.connect_btn.configure(
                text="Connect to Peers", 
                fg_color="black", # Default CTK color
                command=lambda: launch_connection(sock, control_port, state, view)
            )
            print("[python] Connection attempt cancelled by user.")

def main() -> None:
    py_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    py_sock.bind((HOST, 0))
    py_port = py_sock.getsockname()[1]
    print(f"[python] listening on [{HOST}]:{py_port}")
    print(f"[python] using C bridge executable: {C_EXE_PATH}")

    # CREATE THE GUI
    app = messaging_app()

    state = SharedState()
    threading.Thread(target=receiver_thread, args=(py_sock, state, app), daemon=True).start()

    try:
        proc = subprocess.Popen([C_EXE_PATH, str(py_port)])
        control_port = wait_for_control_port(state, proc)
    except FileNotFoundError:
        print("[python] Could not start the C bridge executable.")
        print("[python] Expected to find it here:")
        print(f"         {C_EXE_PATH}")
        print("[python] On Windows, compile the C source to contest_pybridge.exe first.")
        py_sock.close()
        return

    print(f"[python] started C bridge pid={proc.pid}")

    try:
        # request_mode_and_endpoint(py_sock, control_port, state) --- the gui command askes for this now


        # --- GUI CALLBACKS ---

        def handle_file_transfer():
            chosen = open_file_picker()
            if not chosen:
                print("[python] No file selected.")
            if not os.path.isfile(chosen):
                print("[python] Selected path is not a regular file.")

            try:
                # Run file sending in a thread so the GUI doesn't "freeze" during large transfers
                threading.Thread(target=send_file_over_existing_link, 
                                 args=(py_sock, control_port, chosen), 
                                 daemon=True).start()
            except Exception as exc:
                print(f"[python] File send failed: {exc}")

        def on_closing():
            # Send the exit packet to the C bridge before killing the app
            try:
                send_packet(py_sock, control_port, PKT_EXIT, "")
                time.sleep(0.25)
            except:
                pass
            app.quit()
            app.destroy()

        app.protocol("WM_DELETE_WINDOW", on_closing)

        # Link the connect to peers button
        app.connect_btn.configure(command=lambda: launch_connection(py_sock, control_port, state, app))
        # Link the send and add file button
        app.send_btn.configure(command=lambda: send_gui_message(py_sock, control_port, app))
        app.file_btn.configure(command=handle_file_transfer)
        # Allow "Enter" key to send messages
        app.bind("<Return>", lambda _: send_gui_message(py_sock, control_port, app))
        

        if proc.poll() is not None:
                print("[python] C bridge has exited.")

        if state.process_should_end:
            print("[python] Session is ending.")

        app.mainloop()


    finally:
        try:
            py_sock.close()
        except OSError:
            pass

        if proc.poll() is None:
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
    
    print("[python] Mainloop ended. Finalizing process termination.")


if __name__ == "__main__":
    main()
    