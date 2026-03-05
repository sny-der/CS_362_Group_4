/*
 * 2ipv6test_windows.c
 *
 * Cross-platform (Windows 10/11 + Linux) version of the Python-controlled IPv6
 * bridge. The packet protocol and behavior are unchanged:
 *   - Python starts this program with ONE argument: the local UDP port Python is
 *     already listening on.
 *   - This C program opens its own loopback control socket, tells Python which
 *     port it chose, then waits for Python to choose local/public mode.
 *   - Python <-> C packets and C <-> C packets both use the same 8-byte type
 *     prefix protocol.
 *   - Chat, EXIT handling, local mode, public mode, and file-transfer payloads
 *     carried inside MSG----- packets all continue to work exactly the same.
 *
 * This file adds only platform-compatibility glue:
 *   - WinSock startup / cleanup on Windows
 *   - closesocket() instead of close() on Windows
 *   - InetPtonA() wrapper on Windows
 *   - GetTickCount64() timing on Windows
 *
 * No protocol changes were made.
 */

import socket
import select
import time
import sys
import struct
import random

CTRL_RX_BUFSZ = 8192
PEER_RX_BUFSZ = 8192
TYPE_LEN = 8

KEEPALIVE_INTERVAL_MS = 15000
INITIAL_PUNCH_COUNT = 5
INITIAL_PUNCH_INTERVAL_MS = 500

PKT_MSG = b"MSG-----"
PKT_EXIT = b"EXIT----"
PKT_INFO = b"INFO----"
PKT_CTLPORT = b"CTLPORT-"
PKT_MYENDP = b"MYENDP--"
PKT_MKLOCAL = b"MKLOCAL-"
PKT_MKPUB = b"MKPUB---"
PKT_SETPEER = b"SETPEER-"
PKT_PING = b"PING----"


MODE_NONE = 0
MODE_LOCAL = 1
MODE_PUBLIC = 2


class AppState:
    def __init__(self):
        self.control_sock = None
        self.peer_sock = None

        self.mode = MODE_NONE
        self.peer_socket_ready = False
        self.remote_peer_ready = False

        self.python_addr = None
        self.remote_peer = None

        self.last_keepalive_ms = 0
        self.last_punch_ms = 0
        self.punches_left = 0


def now_ms():
    return int(time.time() * 1000)


def bind_udp_ipv6(ip, port):
    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((ip, port))
    return s


def send_typed_packet(sock, addr, pkt_type, payload=""):
    if isinstance(payload, str):
        payload = payload.encode()

    data = pkt_type + payload
    sock.sendto(data, addr)


def recv_typed_packet(sock, bufsize):
    data, addr = sock.recvfrom(bufsize)

    if len(data) < TYPE_LEN:
        return None, None, addr

    pkt_type = data[:TYPE_LEN]
    payload = data[TYPE_LEN:].decode(errors="ignore")

    return pkt_type, payload, addr


def notify_python(st, pkt_type, text=""):
    send_typed_packet(st.control_sock, st.python_addr, pkt_type, text)


def notify_info(st, text):
    notify_python(st, PKT_INFO, text)


def parse_endpoint_text(text):
    if not text.startswith("["):
        raise ValueError("Invalid endpoint")

    ip = text[1:text.index("]")]
    port = int(text.split("]:")[1])

    return (ip, port, 0, 0)


def build_shareable_endpoint(sock):
    ip, port, *_ = sock.getsockname()
    return f"[{ip}]:{port}"


def close_peer_socket(st):
    if st.peer_sock:
        st.peer_sock.close()

    st.peer_sock = None
    st.peer_socket_ready = False
    st.remote_peer_ready = False
    st.mode = MODE_NONE
    st.punches_left = 0


def create_peer_socket_local(st):
    close_peer_socket(st)

    st.peer_sock = bind_udp_ipv6("::1", 0)

    st.mode = MODE_LOCAL
    st.peer_socket_ready = True

    endpoint = build_shareable_endpoint(st.peer_sock)

    notify_python(st, PKT_MYENDP, endpoint)
    notify_info(st, "Local loopback peer socket created.")


def create_peer_socket_public(st):
    close_peer_socket(st)

    st.peer_sock = bind_udp_ipv6("::", 0)

    st.mode = MODE_PUBLIC
    st.peer_socket_ready = True

    endpoint = build_shareable_endpoint(st.peer_sock)

    notify_python(st, PKT_MYENDP, endpoint)
    notify_info(st, "Public peer socket created.")


def schedule_initial_punches(st):
    st.punches_left = INITIAL_PUNCH_COUNT
    st.last_punch_ms = 0
    st.last_keepalive_ms = now_ms()


def handle_python_packet(st, pkt_type, payload):
    if pkt_type == PKT_MKLOCAL:
        create_peer_socket_local(st)
        return False

    if pkt_type == PKT_MKPUB:
        create_peer_socket_public(st)
        return False

    if pkt_type == PKT_SETPEER:
        st.remote_peer = parse_endpoint_text(payload)
        st.remote_peer_ready = True
        schedule_initial_punches(st)

        notify_info(st, "Remote peer endpoint saved.")
        return False

    if pkt_type == PKT_MSG:
        if not st.remote_peer_ready:
            notify_info(st, "Peer not configured")
            return False

        send_typed_packet(st.peer_sock, st.remote_peer, PKT_MSG, payload)
        return False

    if pkt_type == PKT_EXIT:
        if st.remote_peer_ready:
            send_typed_packet(st.peer_sock, st.remote_peer, PKT_EXIT)

        notify_info(st, "Local user requested exit.")
        return True

    return False


def handle_peer_packet(st, pkt_type, payload):
    if pkt_type == PKT_MSG:
        notify_python(st, PKT_MSG, payload)

    elif pkt_type == PKT_EXIT:
        notify_info(st, "Remote peer ended session.")
        return True

    return False


def maybe_send_periodic(st):
    if not st.peer_socket_ready or not st.remote_peer_ready:
        return

    now = now_ms()

    if st.punches_left > 0:
        if st.last_punch_ms == 0 or now - st.last_punch_ms >= INITIAL_PUNCH_INTERVAL_MS:
            send_typed_packet(st.peer_sock, st.remote_peer, PKT_PING, "hello")
            st.last_punch_ms = now
            st.punches_left -= 1

    if now - st.last_keepalive_ms >= KEEPALIVE_INTERVAL_MS:
        send_typed_packet(st.peer_sock, st.remote_peer, PKT_PING, "keepalive")
        st.last_keepalive_ms = now


def run_bridge_loop(st):
    while True:
        rlist = [st.control_sock]

        if st.peer_socket_ready:
            rlist.append(st.peer_sock)

        readable, _, _ = select.select(rlist, [], [], 0.25)

        for sock in readable:

            if sock == st.control_sock:
                pkt_type, payload, _ = recv_typed_packet(sock, CTRL_RX_BUFSZ)

                if pkt_type:
                    if handle_python_packet(st, pkt_type, payload):
                        return

            elif sock == st.peer_sock:
                pkt_type, payload, _ = recv_typed_packet(sock, PEER_RX_BUFSZ)

                if pkt_type:
                    if handle_peer_packet(st, pkt_type, payload):
                        return

        maybe_send_periodic(st)


def main():
    if len(sys.argv) != 2:
        print("Usage: python bridge.py <python_recv_port>")
        return

    py_port = int(sys.argv[1])

    st = AppState()

    st.python_addr = ("::1", py_port, 0, 0)

    st.control_sock = bind_udp_ipv6("::1", 0)

    ctl_port = st.control_sock.getsockname()[1]

    notify_python(st, PKT_CTLPORT, str(ctl_port))
    notify_info(st, "Bridge started.")

    run_bridge_loop(st)


if __name__ == "__main__":
    main()
