#!/usr/bin/env python3
from __future__ import annotations

import threading
import time

import model.NetworkMain_windows as bridge


class Poller:
    def __init__(self) -> None:
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        self.thread.join(timeout=1)

    def _run(self) -> None:
        while not self.stop_event.is_set():
            msg = bridge.read_inc_message_queue()
            if msg and not msg.startswith("ERROR:"):
                print(f"\npeer> {msg}")
            time.sleep(0.2)


def main() -> None:
    startup_result = bridge.startup()
    if startup_result is None:
        return

    send_to_control, receive_from_python = startup_result
    print(f"Python -> C control target: {send_to_control}")
    print(f"C -> Python receive target: {receive_from_python}")

    while True:
        mode = input("Choose mode: local or public? ").strip().lower()
        if mode in {"local", "l"}:
            local_endpoint = bridge.prepare_local_endpoint()
            if local_endpoint.startswith("ERROR:"):
                return
            print(f"Your shareable endpoint: {local_endpoint}")
            other = input("Enter the OTHER terminal's port (or [::1]:port): ").strip()
            if bridge.establish_local_connection(other) is None:
                return
            break
        if mode in {"public", "p"}:
            local_endpoint = bridge.prepare_public_endpoint()
            if local_endpoint.startswith("ERROR:"):
                return
            print(f"Share this endpoint with the other device: {local_endpoint}")
            other = input("Enter the OTHER device's [ipv6]:port: ").strip()
            if bridge.establish_connection(other) is None:
                return
            break
        print("Please type 'local' or 'public'.")

    poller = Poller()
    poller.start()

    try:
        while True:
            line = input("py> ").strip()
            if line == "exit":
                bridge.terminate_program()
                break
            if line == "file":
                chosen = input("File path: ").strip()
                if chosen:
                    bridge.send_file(chosen)
                continue
            bridge.send_message(line)
    finally:
        poller.stop()


if __name__ == "__main__":
    main()