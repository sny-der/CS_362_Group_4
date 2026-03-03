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

#if !defined(_WIN32)
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
typedef SOCKET socket_handle_t;
typedef SSIZE_T ssize_t;
#ifndef EPROTO
#define EPROTO WSAEPROTONOSUPPORT
#endif
#ifndef EMSGSIZE
#define EMSGSIZE WSAEMSGSIZE
#endif
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int socket_handle_t;
#endif

/* ------------------------------ Tunables ------------------------------- */
#define CTRL_RX_BUFSZ             8192
#define PEER_RX_BUFSZ             8192
#define TYPE_LEN                  8
#define KEEPALIVE_INTERVAL_MS     15000
#define INITIAL_PUNCH_COUNT       5
#define INITIAL_PUNCH_INTERVAL_MS 500

/* -------------------------- 8-byte packet types ------------------------ */
#define PKT_MSG      "MSG-----"
#define PKT_EXIT     "EXIT----"
#define PKT_INFO     "INFO----"
#define PKT_CTLPORT  "CTLPORT-"
#define PKT_MYENDP   "MYENDP--"
#define PKT_MKLOCAL  "MKLOCAL-"
#define PKT_MKPUB    "MKPUB---"
#define PKT_SETPEER  "SETPEER-"
#define PKT_PING     "PING----"

/* -------------------------- Helper structures -------------------------- */
typedef enum {
    MODE_NONE = 0,
    MODE_LOCAL,
    MODE_PUBLIC
} peer_mode_t;

typedef struct {
    socket_handle_t control_sock;
    socket_handle_t peer_sock;
    peer_mode_t mode;
    int peer_socket_ready;
    int remote_peer_ready;
    struct sockaddr_in6 python_addr;
    struct sockaddr_in6 remote_peer;
    long long last_keepalive_ms;
    long long last_punch_ms;
    int punches_left;
} app_state_t;

/* -------------------------- Platform helpers -------------------------- */
#ifdef _WIN32
static int sockets_init(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }
    return 0;
}

static void sockets_cleanup(void) {
    WSACleanup();
}

static void close_socket(socket_handle_t s) {
    if (s != INVALID_SOCKET) {
        closesocket(s);
    }
}

static int socket_is_valid(socket_handle_t s) {
    return s != INVALID_SOCKET;
}

static void print_socket_error(const char *label) {
    fprintf(stderr, "%s failed: WSA error %d\n", label, WSAGetLastError());
}

static int inet_pton6(const char *text, struct in6_addr *out) {
    return InetPtonA(AF_INET6, text, out);
}

static long long now_ms(void) {
    return (long long)GetTickCount64();
}

static const char *gai_strerror_portable(int rc) {
    return gai_strerrorA(rc);
}
#else
static int sockets_init(void) {
    return 0;
}

static void sockets_cleanup(void) {
}

static void close_socket(socket_handle_t s) {
    if (s >= 0) {
        close(s);
    }
}

static int socket_is_valid(socket_handle_t s) {
    return s >= 0;
}

static void print_socket_error(const char *label) {
    perror(label);
}

static int inet_pton6(const char *text, struct in6_addr *out) {
    return inet_pton(AF_INET6, text, out);
}

static long long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
}

static const char *gai_strerror_portable(int rc) {
    return gai_strerror(rc);
}
#endif

/* ------------------------- IPv6 formatting helpers --------------------- */
static int ipv6_to_full(const struct in6_addr *a, char *buf, size_t buflen) {
    if (buflen < 40) return -1;

    int off = 0;
    for (int i = 0; i < 8; i++) {
        uint16_t seg = (uint16_t)((a->s6_addr[i * 2] << 8) | a->s6_addr[i * 2 + 1]);
        int n = snprintf(buf + off, buflen - (size_t)off, "%s%04x", (i ? ":" : ""), seg);
        if (n < 0) return -1;
        off += n;
        if ((size_t)off >= buflen) return -1;
    }
    return 0;
}

static void format_sockaddr6_full(const struct sockaddr_in6 *sa6, char *buf, size_t buflen) {
    char ip_full[64];
    if (ipv6_to_full(&sa6->sin6_addr, ip_full, sizeof(ip_full)) != 0) {
        snprintf(buf, buflen, "[format-error]:%u", ntohs(sa6->sin6_port));
        return;
    }
    snprintf(buf, buflen, "[%s]:%u", ip_full, ntohs(sa6->sin6_port));
}

/* ---------------------- Typed packet encode / decode ------------------- */
static int send_typed_packet_text(socket_handle_t sock,
                                  const struct sockaddr_in6 *to,
                                  const char type[TYPE_LEN + 1],
                                  const char *payload) {
    uint8_t buf[CTRL_RX_BUFSZ];
    size_t payload_len = payload ? strlen(payload) : 0;
    size_t total = TYPE_LEN + payload_len;

    if (total > sizeof(buf)) {
        errno = EMSGSIZE;
        return -1;
    }

    memcpy(buf, type, TYPE_LEN);
    if (payload_len > 0) {
        memcpy(buf + TYPE_LEN, payload, payload_len);
    }

    ssize_t sent = sendto(sock, (const char *)buf, (int)total, 0,
                          (const struct sockaddr *)to, (int)sizeof(*to));
    if (sent != (ssize_t)total) return -1;
    return 0;
}

static int recv_typed_packet_text(socket_handle_t sock,
                                  char out_type[TYPE_LEN + 1],
                                  char *out_payload,
                                  size_t out_payload_sz,
                                  struct sockaddr_in6 *from,
                                  socklen_t *fromlen) {
    uint8_t buf[CTRL_RX_BUFSZ];
    ssize_t n = recvfrom(sock, (char *)buf, (int)sizeof(buf), 0,
                         (struct sockaddr *)from, fromlen);
    if (n < 0) return -1;
    if (n < TYPE_LEN) {
        errno = EPROTO;
        return -1;
    }

    memcpy(out_type, buf, TYPE_LEN);
    out_type[TYPE_LEN] = '\0';

    size_t payload_len = (size_t)n - TYPE_LEN;
    if (payload_len >= out_payload_sz) payload_len = out_payload_sz - 1;
    if (payload_len > 0) {
        memcpy(out_payload, buf + TYPE_LEN, payload_len);
    }
    out_payload[payload_len] = '\0';
    return 0;
}

/* -------------------------- Socket helpers ----------------------------- */
static socket_handle_t bind_udp_ipv6(const char *local_ip, uint16_t port) {
    socket_handle_t s = socket(AF_INET6, SOCK_DGRAM, 0);
    if (!socket_is_valid(s)) {
        print_socket_error("socket(AF_INET6,SOCK_DGRAM)");
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }

    int one = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    (void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&one, sizeof(one));

    struct sockaddr_in6 local;
    memset(&local, 0, sizeof(local));
    local.sin6_family = AF_INET6;
    local.sin6_port = htons(port);
    if (inet_pton6(local_ip, &local.sin6_addr) != 1) {
        close_socket(s);
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -2;
#endif
    }

    if (bind(s, (struct sockaddr *)&local, (int)sizeof(local)) < 0) {
        print_socket_error("bind(UDP IPv6)");
        close_socket(s);
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -3;
#endif
    }

    return s;
}

static int get_local_socket_endpoint(socket_handle_t s, struct sockaddr_in6 *out) {
    socklen_t len = (socklen_t)sizeof(*out);
    if (getsockname(s, (struct sockaddr *)out, &len) < 0) {
        print_socket_error("getsockname");
        return -1;
    }
    return 0;
}

static int parse_endpoint_text(const char *text, struct sockaddr_in6 *out) {
    if (!text || text[0] != '[') return -1;

    const char *rb = strchr(text, ']');
    if (!rb) return -1;
    if (rb[1] != ':') return -1;

    char ip[256];
    char port_s[64];

    size_t ip_len = (size_t)(rb - (text + 1));
    if (ip_len == 0 || ip_len >= sizeof(ip)) return -1;
    memcpy(ip, text + 1, ip_len);
    ip[ip_len] = '\0';

    strncpy(port_s, rb + 2, sizeof(port_s) - 1);
    port_s[sizeof(port_s) - 1] = '\0';

    char *end = NULL;
    unsigned long port_ul = strtoul(port_s, &end, 10);
    if (!end || *end != '\0' || port_ul > 65535UL) return -1;

    memset(out, 0, sizeof(*out));
    out->sin6_family = AF_INET6;
    out->sin6_port = htons((uint16_t)port_ul);

    if (inet_pton6(ip, &out->sin6_addr) != 1) return -1;
    return 0;
}

/* ----------------------- Route-selected source IPv6 -------------------- */
static int chosen_source_ipv6(struct in6_addr *out_addr) {
    const char *dst_ip = "2606:4700:4700::1111";
    const uint16_t dst_port = 53;

    socket_handle_t s = socket(AF_INET6, SOCK_DGRAM, 0);
    if (!socket_is_valid(s)) return -1;

    struct sockaddr_in6 dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin6_family = AF_INET6;
    dst.sin6_port = htons(dst_port);
    if (inet_pton6(dst_ip, &dst.sin6_addr) != 1) {
        close_socket(s);
        return -2;
    }

    if (connect(s, (struct sockaddr *)&dst, (int)sizeof(dst)) < 0) {
        close_socket(s);
        return -3;
    }

    struct sockaddr_in6 local;
    socklen_t len = (socklen_t)sizeof(local);
    if (getsockname(s, (struct sockaddr *)&local, &len) < 0) {
        close_socket(s);
        return -4;
    }

    close_socket(s);
    if (out_addr) *out_addr = local.sin6_addr;
    return 0;
}

/* --------------------------- STUN helpers ------------------------------ */
#define STUN_BINDING_REQUEST         0x0001
#define STUN_BINDING_SUCCESS         0x0101
#define STUN_MAGIC_COOKIE            0x2112A442u
#define STUN_ATTR_MAPPED_ADDRESS     0x0001
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xff);
    p[1] = (uint8_t)(v & 0xff);
}

static void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

static int random_bytes(uint8_t *dst, size_t n) {
    /* STUN transaction IDs do not need cryptographic strength here. */
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)time(NULL));
    }

    for (size_t i = 0; i < n; i++) {
        dst[i] = (uint8_t)(rand() & 0xff);
    }
    return 0;
}

static int resolve_stun_server_ipv6(const char *host, const char *port, struct sockaddr_in6 *out) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(STUN %s): %s\n", host, gai_strerror_portable(rc));
        return -1;
    }

    int found = 0;
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        if ((size_t)rp->ai_addrlen >= sizeof(struct sockaddr_in6)) {
            memcpy(out, rp->ai_addr, sizeof(struct sockaddr_in6));
            found = 1;
            break;
        }
    }

    freeaddrinfo(res);
    return found ? 0 : -2;
}

static int stun_ipv6_mapped_on_socket(socket_handle_t s,
                                      const struct sockaddr_in6 *stun_addr,
                                      struct in6_addr *mapped_addr,
                                      uint16_t *mapped_port) {
    uint8_t txid[12];
    if (random_bytes(txid, sizeof(txid)) != 0) return -1;

    uint8_t req[20];
    write_be16(req + 0, STUN_BINDING_REQUEST);
    write_be16(req + 2, 0);
    write_be32(req + 4, STUN_MAGIC_COOKIE);
    memcpy(req + 8, txid, sizeof(txid));

    for (int attempt = 0; attempt < 3; attempt++) {
        if (sendto(s, (const char *)req, (int)sizeof(req), 0,
                   (const struct sockaddr *)stun_addr, (int)sizeof(*stun_addr)) != (ssize_t)sizeof(req)) {
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

#ifdef _WIN32
        int sel = select(0, &rfds, NULL, NULL, &tv);
#else
        int sel = select((int)(s + 1), &rfds, NULL, NULL, &tv);
#endif
        if (sel <= 0) continue;

        uint8_t resp[1500];
        struct sockaddr_in6 from;
        socklen_t from_len = (socklen_t)sizeof(from);
        ssize_t n = recvfrom(s, (char *)resp, (int)sizeof(resp), 0,
                             (struct sockaddr *)&from, &from_len);
        if (n < 20) continue;

        uint16_t msg_type = read_be16(resp + 0);
        uint16_t msg_len  = read_be16(resp + 2);
        uint32_t cookie   = read_be32(resp + 4);

        if (cookie != STUN_MAGIC_COOKIE) continue;
        if (msg_type != STUN_BINDING_SUCCESS) continue;
        if (20 + (ssize_t)msg_len > n) continue;
        if (memcmp(resp + 8, txid, sizeof(txid)) != 0) continue;

        size_t pos = 20;
        int found = 0;
        struct in6_addr got_addr;
        uint16_t got_port = 0;

        while (pos + 4 <= (size_t)n) {
            uint16_t at = read_be16(resp + pos);
            uint16_t al = read_be16(resp + pos + 2);
            pos += 4;
            if (pos + al > (size_t)n) break;

            const uint8_t *val = resp + pos;

            if (at == STUN_ATTR_XOR_MAPPED_ADDRESS && al >= 20 && val[1] == 0x02) {
                uint16_t xport = read_be16(val + 2);
                got_port = (uint16_t)(xport ^ (uint16_t)(STUN_MAGIC_COOKIE >> 16));

                uint8_t key[16];
                key[0] = 0x21; key[1] = 0x12; key[2] = 0xA4; key[3] = 0x42;
                memcpy(key + 4, txid, 12);
                for (int i = 0; i < 16; i++) {
                    got_addr.s6_addr[i] = (uint8_t)(val[4 + i] ^ key[i]);
                }
                found = 1;
                break;
            }

            if (at == STUN_ATTR_MAPPED_ADDRESS && al >= 20 && val[1] == 0x02 && !found) {
                got_port = read_be16(val + 2);
                memcpy(got_addr.s6_addr, val + 4, 16);
                found = 1;
            }

            pos += (al + 3u) & ~3u;
        }

        if (!found) continue;
        if (mapped_addr) *mapped_addr = got_addr;
        if (mapped_port) *mapped_port = got_port;
        return 0;
    }

    return -2;
}

/* ---------------------- Python notification helpers -------------------- */
static void notify_python(app_state_t *st, const char type[TYPE_LEN + 1], const char *text) {
    if (send_typed_packet_text(st->control_sock, &st->python_addr, type, text ? text : "") != 0) {
        print_socket_error("sendto(Python control packet)");
    }
}

static void notify_info(app_state_t *st, const char *text) {
    notify_python(st, PKT_INFO, text);
}

/* --------------------- Peer socket creation helpers -------------------- */
static void close_peer_socket_if_open(app_state_t *st) {
    if (socket_is_valid(st->peer_sock)) {
        close_socket(st->peer_sock);
#ifdef _WIN32
        st->peer_sock = INVALID_SOCKET;
#else
        st->peer_sock = -1;
#endif
    }
    st->peer_socket_ready = 0;
    st->remote_peer_ready = 0;
    st->mode = MODE_NONE;
    st->punches_left = 0;
}

static int build_shareable_endpoint_text(app_state_t *st, char *out, size_t out_sz) {
    struct sockaddr_in6 bound_local;
    if (get_local_socket_endpoint(st->peer_sock, &bound_local) != 0) return -1;

    if (st->mode == MODE_LOCAL) {
        format_sockaddr6_full(&bound_local, out, out_sz);
        return 0;
    }

    struct sockaddr_in6 stun_addr;
    if (resolve_stun_server_ipv6("stun.cloudflare.com", "3478", &stun_addr) == 0) {
        struct in6_addr mapped_addr;
        uint16_t mapped_port = 0;
        if (stun_ipv6_mapped_on_socket(st->peer_sock, &stun_addr, &mapped_addr, &mapped_port) == 0) {
            struct sockaddr_in6 pub;
            memset(&pub, 0, sizeof(pub));
            pub.sin6_family = AF_INET6;
            pub.sin6_addr = mapped_addr;
            pub.sin6_port = htons(mapped_port);
            format_sockaddr6_full(&pub, out, out_sz);
            return 0;
        }
    }

    struct in6_addr src;
    if (chosen_source_ipv6(&src) == 0) {
        struct sockaddr_in6 guess;
        memset(&guess, 0, sizeof(guess));
        guess.sin6_family = AF_INET6;
        guess.sin6_addr = src;
        guess.sin6_port = bound_local.sin6_port;
        format_sockaddr6_full(&guess, out, out_sz);
        return 0;
    }

    format_sockaddr6_full(&bound_local, out, out_sz);
    return 0;
}

static int create_peer_socket_local(app_state_t *st) {
    close_peer_socket_if_open(st);

    st->peer_sock = bind_udp_ipv6("::1", 0);
    if (!socket_is_valid(st->peer_sock)) return -1;

    st->mode = MODE_LOCAL;
    st->peer_socket_ready = 1;
    st->remote_peer_ready = 0;

    char endpoint[128];
    if (build_shareable_endpoint_text(st, endpoint, sizeof(endpoint)) != 0) return -2;
    notify_python(st, PKT_MYENDP, endpoint);
    notify_info(st, "Local loopback peer socket created. Give the displayed port to the other terminal.");
    return 0;
}

static int create_peer_socket_public(app_state_t *st) {
    close_peer_socket_if_open(st);

    st->peer_sock = bind_udp_ipv6("::", 0);
    if (!socket_is_valid(st->peer_sock)) return -1;

    st->mode = MODE_PUBLIC;
    st->peer_socket_ready = 1;
    st->remote_peer_ready = 0;

    char endpoint[128];
    if (build_shareable_endpoint_text(st, endpoint, sizeof(endpoint)) != 0) return -2;
    notify_python(st, PKT_MYENDP, endpoint);
    notify_info(st, "Public peer socket created. Exchange the displayed [ipv6]:port text with the other device.");
    return 0;
}

/* -------------------------- Control handlers --------------------------- */
static void schedule_initial_punches(app_state_t *st) {
    st->punches_left = INITIAL_PUNCH_COUNT;
    st->last_punch_ms = 0;
    st->last_keepalive_ms = now_ms();
}

static void handle_python_control_packet(app_state_t *st,
                                         const char type[TYPE_LEN + 1],
                                         const char *payload,
                                         int *should_exit) {
    if (strcmp(type, PKT_MKLOCAL) == 0) {
        if (create_peer_socket_local(st) != 0) {
            notify_info(st, "Failed to create local peer socket.");
        }
        return;
    }

    if (strcmp(type, PKT_MKPUB) == 0) {
        if (create_peer_socket_public(st) != 0) {
            notify_info(st, "Failed to create public peer socket.");
        }
        return;
    }

    if (strcmp(type, PKT_SETPEER) == 0) {
        if (!st->peer_socket_ready) {
            notify_info(st, "Create the peer socket first (local or public) before setting the remote peer.");
            return;
        }
        if (parse_endpoint_text(payload, &st->remote_peer) != 0) {
            notify_info(st, "Remote peer endpoint text was invalid. Expected [ipv6]:port.");
            return;
        }
        st->remote_peer_ready = 1;
        schedule_initial_punches(st);
        notify_info(st, "Remote peer endpoint saved. You can now chat.");
        return;
    }

    if (strcmp(type, PKT_MSG) == 0) {
        if (!st->peer_socket_ready || !st->remote_peer_ready) {
            notify_info(st, "Peer session is not configured yet.");
            return;
        }
        if (send_typed_packet_text(st->peer_sock, &st->remote_peer, PKT_MSG, payload) != 0) {
            notify_info(st, "Failed to send chat packet to remote peer.");
        }
        return;
    }

    if (strcmp(type, PKT_EXIT) == 0) {
        if (st->peer_socket_ready && st->remote_peer_ready) {
            (void)send_typed_packet_text(st->peer_sock, &st->remote_peer, PKT_EXIT, "");
        }
        notify_info(st, "Local user requested exit. Closing this bridge.");
        *should_exit = 1;
        return;
    }

    notify_info(st, "Unknown Python control packet type received.");
}

static void handle_peer_packet(app_state_t *st,
                               const char type[TYPE_LEN + 1],
                               const char *payload,
                               int *should_exit) {
    if (strcmp(type, PKT_MSG) == 0) {
        notify_python(st, PKT_MSG, payload);
        return;
    }

    if (strcmp(type, PKT_EXIT) == 0) {
        notify_info(st, "The remote peer ended the session. Closing this bridge.");
        *should_exit = 1;
        return;
    }

    if (strcmp(type, PKT_PING) == 0) {
        return;
    }
}

/* ---------------------------- Main event loop -------------------------- */
static void maybe_send_periodic_peer_packets(app_state_t *st) {
    if (!st->peer_socket_ready || !st->remote_peer_ready) return;

    long long now = now_ms();

    if (st->punches_left > 0) {
        if (st->last_punch_ms == 0 || (now - st->last_punch_ms) >= INITIAL_PUNCH_INTERVAL_MS) {
            (void)send_typed_packet_text(st->peer_sock, &st->remote_peer, PKT_PING, "hello");
            st->last_punch_ms = now;
            st->punches_left--;
        }
    }

    if ((now - st->last_keepalive_ms) >= KEEPALIVE_INTERVAL_MS) {
        (void)send_typed_packet_text(st->peer_sock, &st->remote_peer, PKT_PING, "keepalive");
        st->last_keepalive_ms = now;
    }
}

static int run_bridge_loop(app_state_t *st) {
    int should_exit = 0;

    while (!should_exit) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(st->control_sock, &rfds);

#ifndef _WIN32
        int maxfd = (int)st->control_sock;
#endif

        if (st->peer_socket_ready) {
            FD_SET(st->peer_sock, &rfds);
#ifndef _WIN32
            if ((int)st->peer_sock > maxfd) maxfd = (int)st->peer_sock;
#endif
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000;

#ifdef _WIN32
        int sel = select(0, &rfds, NULL, NULL, &tv);
#else
        int sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);
#endif
        if (sel < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            print_socket_error("select");
            return 1;
        }

        if (sel > 0) {
            if (FD_ISSET(st->control_sock, &rfds)) {
                char type[TYPE_LEN + 1];
                char payload[CTRL_RX_BUFSZ];
                struct sockaddr_in6 from;
                socklen_t fromlen = (socklen_t)sizeof(from);

                if (recv_typed_packet_text(st->control_sock, type, payload, sizeof(payload), &from, &fromlen) == 0) {
                    handle_python_control_packet(st, type, payload, &should_exit);
                }
            }

            if (st->peer_socket_ready && FD_ISSET(st->peer_sock, &rfds)) {
                char type[TYPE_LEN + 1];
                char payload[PEER_RX_BUFSZ];
                struct sockaddr_in6 from;
                socklen_t fromlen = (socklen_t)sizeof(from);

                if (recv_typed_packet_text(st->peer_sock, type, payload, sizeof(payload), &from, &fromlen) == 0) {
                    handle_peer_packet(st, type, payload, &should_exit);
                }
            }
        }

        maybe_send_periodic_peer_packets(st);
    }

    return 0;
}

/* -------------------------------- main --------------------------------- */
int main(int argc, char **argv) {
    if (sockets_init() != 0) {
        return 1;
    }

    int rc = 1;
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <python_recv_port>\n", argv[0]);
        sockets_cleanup();
        return 1;
    }

    char *end = NULL;
    unsigned long py_port_ul = strtoul(argv[1], &end, 10);
    if (!end || *end != '\0' || py_port_ul == 0 || py_port_ul > 65535UL) {
        fprintf(stderr, "Invalid python_recv_port: %s\n", argv[1]);
        sockets_cleanup();
        return 1;
    }

    app_state_t st;
    memset(&st, 0, sizeof(st));
#ifdef _WIN32
    st.control_sock = INVALID_SOCKET;
    st.peer_sock = INVALID_SOCKET;
#else
    st.control_sock = -1;
    st.peer_sock = -1;
#endif
    st.mode = MODE_NONE;

    memset(&st.python_addr, 0, sizeof(st.python_addr));
    st.python_addr.sin6_family = AF_INET6;
    st.python_addr.sin6_port = htons((uint16_t)py_port_ul);
    if (inet_pton6("::1", &st.python_addr.sin6_addr) != 1) {
        fprintf(stderr, "Failed to build Python address ::1\n");
        sockets_cleanup();
        return 1;
    }

    st.control_sock = bind_udp_ipv6("::1", 0);
    if (!socket_is_valid(st.control_sock)) {
        sockets_cleanup();
        return 1;
    }

    struct sockaddr_in6 control_local;
    if (get_local_socket_endpoint(st.control_sock, &control_local) != 0) {
        close_socket(st.control_sock);
        sockets_cleanup();
        return 1;
    }

    char ctl_port_text[32];
    snprintf(ctl_port_text, sizeof(ctl_port_text), "%u", ntohs(control_local.sin6_port));
    notify_python(&st, PKT_CTLPORT, ctl_port_text);
    notify_info(&st, "Bridge started. Choose local or public mode in Python.");

    rc = run_bridge_loop(&st);

    close_peer_socket_if_open(&st);
    close_socket(st.control_sock);
    sockets_cleanup();
    return rc;
}