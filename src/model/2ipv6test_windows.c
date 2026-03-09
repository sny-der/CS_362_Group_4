/*
  2ipv6test_bitmap_windows.c  (Windows 10/11)

  Windows port of the "bitmap + fixed 1200B packets + dynamic pacing" IPv6 UDP bridge.

  Goals preserved from the Linux version:
    - Python control protocol unchanged (8-byte types): CTLPORT-, INFO----, MYENDP--, etc.
    - Peer protocol unchanged (12-byte types), fixed-size peer packets: 1200 bytes total UDP payload.
    - File transfer with:
        * metadata (FILEMETA----)
        * chunk packets (FILECHNK----) using chunk index + total + data_len
        * missing requests (FILEREQ-----)
        * completion (FILEDONE----)
        * confirmation (FILECONF----)
    - File-path FIFO queue (doubly linked list) for multiple outgoing files.
    - Priority message queue (doubly linked list) so chat messages jump ahead of file packets.
    - Optional multithreading:
        * main thread: Python control + keepalive/punch
        * sender thread: send messages and file packets, handle resend requests, pacing
        * peer receiver thread: recv packets, write file via bitmap, request missing, resend DONE
    - No arbitrary per-packet sleep; uses dynamic backpressure (send bursts + backoff on ENOBUFS/EWOULDBLOCK).

  Dependencies (minimal):
    - WinSock2 / ws2tcpip
    - Windows threading primitives (CreateThread, CRITICAL_SECTION, CONDITION_VARIABLE)
    - Windows file I/O (CreateFile, SetFilePointerEx, ReadFile, WriteFile)

  Compile (MSVC):
    cl /O2 /W4 /D_CRT_SECURE_NO_WARNINGS 2ipv6test_bitmap_windows.c ws2_32.lib

  NOTE:
    - Local mode (::1) is treated as an "internet simulation": we still use 1200B packets.
*/

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

/* ============================== Sizes ============================== */

#define CTRL_RX_BUFSZ              8192
#define CTRL_TYPE_LEN              8

#define PEER_TYPE_LEN              12
#define PEER_PKT_SIZE              1200
#define PEER_PAYLOAD_MAX           (PEER_PKT_SIZE - PEER_TYPE_LEN)

/* Peer message payload: u16 msg_len | msg bytes */
#define PEERMSG_HDR_LEN            2

/* Peer-file chunk payload:
     transfer_id[16] | chunk_index[4] | total_chunks[4] | data_len[2] | data[<=DATA_MAX]
*/
#define TRANSFER_ID_LEN            16
#define CHUNK_HEADER_LEN           (TRANSFER_ID_LEN + 4 + 4 + 2)
#define FILE_CHUNK_DATA_MAX        (PEER_PAYLOAD_MAX - CHUNK_HEADER_LEN) /* 1188-26 = 1162 */

#define KEEPALIVE_INTERVAL_MS      15000
#define INITIAL_PUNCH_COUNT        5
#define INITIAL_PUNCH_INTERVAL_MS  500

#define MISSING_CHECK_INTERVAL_MS  300
#define DONE_RESEND_INTERVAL_MS    500
#define META_RESEND_INTERVAL_MS    1000

#define MAX_MESSAGE_STORE          4096
#define MAX_SHAREABLE_ENDPOINT     160
#define PATH_MAX_CHARS             4096
#define RECEIVED_DIR_NAME          "ReceivedFiles"

#define MAX_REQ_BATCH              64
#define MAX_MISSING_SCAN_STEPS     8192

/* dynamic pacing */
#define SEND_BURST_START           128
#define SEND_BURST_MAX             2048
#define SEND_BURST_MIN             1
#define BACKOFF_US_MIN             0
#define BACKOFF_US_MAX             5000

/* ============================== Python control types (8 bytes) ============================== */
#define PKT_MSG      "MSG-----"
#define PKT_EXIT     "EXIT----"
#define PKT_INFO     "INFO----"
#define PKT_CTLPORT  "CTLPORT-"
#define PKT_MYENDP   "MYENDP--"
#define PKT_MKLOCAL  "MKLOCAL-"
#define PKT_MKPUB    "MKPUB---"
#define PKT_SETPEER  "SETPEER-"
#define PKT_SNDFILE  "SNDFILE-"
#define PKT_GETMSG   "GETMSG--"
#define PKT_GETENDP  "GETENDP-"

/* ============================== Peer types (12 bytes) ============================== */
#define PEER_MSG       "TEXTMSG-----"
#define PEER_EXIT      "EXITPEER----"
#define PEER_PING      "PINGPEER----"
#define PEER_FILEMETA  "FILEMETA----"
#define PEER_FILECHNK  "FILECHNK----"
#define PEER_FILEREQ   "FILEREQ-----"
#define PEER_FILEDONE  "FILEDONE----"
#define PEER_FILECONF  "FILECONF----"

/* ============================== Helpers ============================== */

typedef enum {
    MODE_NONE = 0,
    MODE_LOCAL,
    MODE_PUBLIC
} peer_mode_t;

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8)  | (uint64_t)p[7];
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
static void write_be64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)((v >> 56) & 0xff);
    p[1] = (uint8_t)((v >> 48) & 0xff);
    p[2] = (uint8_t)((v >> 40) & 0xff);
    p[3] = (uint8_t)((v >> 32) & 0xff);
    p[4] = (uint8_t)((v >> 24) & 0xff);
    p[5] = (uint8_t)((v >> 16) & 0xff);
    p[6] = (uint8_t)((v >> 8) & 0xff);
    p[7] = (uint8_t)(v & 0xff);
}

static long long now_ms(void) {
    return (long long)GetTickCount64();
}

/* Backoff uses microseconds in config, but Windows Sleep is millisecond resolution. */
static void sleep_us(int us) {
    if (us <= 0) return;
    DWORD ms = (DWORD)((us + 999) / 1000);
    Sleep(ms);
}

static void print_wsa_error(const char *label) {
    fprintf(stderr, "%s failed: WSA error %d\n", label, WSAGetLastError());
}

/* Map common WSA errors into errno-style codes the pacing logic checks. */
#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef ENOBUFS
#define ENOBUFS 105
#endif
#ifndef EMSGSIZE
#define EMSGSIZE 90
#endif
#ifndef EPROTO
#define EPROTO 71
#endif

static void set_errno_from_wsa(int wsa_err) {
    /* Keep it small; we only really care about backpressure errors. */
    if (wsa_err == WSAEWOULDBLOCK) {
        errno = EWOULDBLOCK;
    } else if (wsa_err == WSAENOBUFS) {
        errno = ENOBUFS;
    } else if (wsa_err == WSAEMSGSIZE) {
        errno = EMSGSIZE;
    } else {
        errno = wsa_err;
    }
}

static size_t strnlen_portable(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n] != '\0') n++;
    return n;
}

/* Minimal random bytes: transfer ids do not need cryptographic strength here. */
static int random_bytes(uint8_t *dst, size_t n) {
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)(GetTickCount64() ^ (uintptr_t)&seeded));
    }
    for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)(rand() & 0xff);
    return 0;
}

/* ============================== Bitmap ============================== */

typedef struct {
    uint64_t *words;
    uint32_t nbits;
    uint32_t nwords;
} bitmap_t;

static uint32_t bitmap_words_for_bits(uint32_t nbits) {
    return (nbits + 63u) / 64u;
}

static int bitmap_init(bitmap_t *bm, uint32_t nbits) {
    memset(bm, 0, sizeof(*bm));
    bm->nbits = nbits;
    bm->nwords = bitmap_words_for_bits(nbits);
    bm->words = (uint64_t *)calloc(bm->nwords ? bm->nwords : 1u, sizeof(uint64_t));
    return bm->words ? 0 : -1;
}

static void bitmap_free(bitmap_t *bm) {
    free(bm->words);
    memset(bm, 0, sizeof(*bm));
}

static __inline void bitmap_set(bitmap_t *bm, uint32_t i) {
    bm->words[i >> 6] |= (1ULL << (i & 63u));
}

static __inline int bitmap_test(const bitmap_t *bm, uint32_t i) {
    return (bm->words[i >> 6] & (1ULL << (i & 63u))) != 0ULL;
}

/* ============================== Doubly linked lists ============================== */

typedef struct file_task {
    struct file_task *prev;
    struct file_task *next;
    char *path;
} file_task_t;

typedef struct send_node {
    struct send_node *prev;
    struct send_node *next;
    char type[PEER_TYPE_LEN + 1];
    size_t payload_len;
    uint8_t *payload;
} send_node_t;

typedef struct resend_node {
    struct resend_node *prev;
    struct resend_node *next;
    uint32_t chunk_index;
} resend_node_t;

/* ============================== Transfer state ============================== */

typedef struct {
    int active;
    HANDLE hfile; /* sender file handle */
    uint8_t transfer_id[TRANSFER_ID_LEN];
    uint64_t filesize;
    uint32_t total_chunks;
    char filename[512];

    uint8_t meta_payload[PEER_PAYLOAD_MAX];
    size_t meta_len;

    uint32_t next_index;
    int sent_all;

    resend_node_t *rq_head;
    resend_node_t *rq_tail;

    int done_seen;
    long long last_meta_send_ms;

    int burst;
    int backoff_us;
} outgoing_transfer_t;

typedef struct {
    int active;
    HANDLE hfile; /* receiver output file handle */
    uint8_t transfer_id[TRANSFER_ID_LEN];
    uint64_t filesize;
    uint32_t total_chunks;
    uint32_t received_chunks;
    char filename[512];
    char output_path[PATH_MAX_CHARS];

    bitmap_t received_bm;
    uint32_t scan_pos;

    long long last_missing_check_ms;

    int done_sent;
    int done_confirmed;
    long long last_done_send_ms;
} incoming_transfer_t;

/* ============================== App State ============================== */

typedef struct {
    SOCKET control_sock;
    SOCKET peer_sock;
    peer_mode_t mode;
    int peer_socket_ready;
    int remote_peer_ready;
    struct sockaddr_in6 python_addr;
    struct sockaddr_in6 remote_peer;
    long long last_keepalive_ms;
    long long last_punch_ms;
    int punches_left;
    char shareable_endpoint[MAX_SHAREABLE_ENDPOINT];

    CRITICAL_SECTION msg_mtx;
    char latest_message[MAX_MESSAGE_STORE];
    int latest_message_ready;

    int threading_enabled;
    volatile LONG stop_flag;
    HANDLE sender_thread;
    HANDLE peer_rx_thread;

    CRITICAL_SECTION peer_mtx;

    /* Sender-work lock protects fileq + sendq. */
    CRITICAL_SECTION work_mtx;
    CONDITION_VARIABLE work_cv;

    file_task_t *fileq_head;
    file_task_t *fileq_tail;

    send_node_t *sendq_head;
    send_node_t *sendq_tail;

    CRITICAL_SECTION out_mtx;
    CONDITION_VARIABLE out_cv;
    outgoing_transfer_t outgoing;

    CRITICAL_SECTION in_mtx;
    incoming_transfer_t incoming;
} app_state_t;

/* ============================== WinSock init ============================== */

static int sockets_init(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }
    return 0;
}

static void sockets_cleanup(void) {
    WSACleanup();
}

static void close_socket(SOCKET s) {
    if (s != INVALID_SOCKET) closesocket(s);
}

static int socket_is_valid(SOCKET s) {
    return s != INVALID_SOCKET;
}

/* ============================== IPv6 formatting ============================== */

static int ipv6_to_full(const struct in6_addr *a, char *buf, size_t buflen) {
    if (buflen < 40) return -1;
    int off = 0;
    for (int i = 0; i < 8; i++) {
        uint16_t seg = (uint16_t)((a->s6_addr[i*2] << 8) | a->s6_addr[i*2+1]);
        int n = _snprintf(buf + off, (int)(buflen - (size_t)off), "%s%04x", (i ? ":" : ""), seg);
        if (n < 0) return -1;
        off += n;
        if ((size_t)off >= buflen) return -1;
    }
    return 0;
}

static void format_sockaddr6_full(const struct sockaddr_in6 *sa6, char *buf, size_t buflen) {
    char ip_full[64];
    if (ipv6_to_full(&sa6->sin6_addr, ip_full, sizeof(ip_full)) != 0) {
        _snprintf(buf, (int)buflen, "[format-error]:%u", ntohs(sa6->sin6_port));
        return;
    }
    _snprintf(buf, (int)buflen, "[%s]:%u", ip_full, ntohs(sa6->sin6_port));
}

/* ============================== Socket helpers ============================== */

static SOCKET bind_udp_ipv6(const char *local_ip, uint16_t port) {
    SOCKET s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (!socket_is_valid(s)) {
        print_wsa_error("socket(AF_INET6,SOCK_DGRAM)");
        return INVALID_SOCKET;
    }

    int one = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    (void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&one, sizeof(one));

    /* Non-blocking is useful to detect EWOULDBLOCK, but we keep blocking sockets for simplicity.
       Backpressure still shows up as WSAENOBUFS sometimes; EWOULDBLOCK mainly happens when nonblocking.
       We'll keep sockets blocking, and pace mostly on ENOBUFS. */

    struct sockaddr_in6 local;
    memset(&local, 0, sizeof(local));
    local.sin6_family = AF_INET6;
    local.sin6_port = htons(port);
    if (InetPtonA(AF_INET6, local_ip, &local.sin6_addr) != 1) {
        close_socket(s);
        return INVALID_SOCKET;
    }

    if (bind(s, (struct sockaddr *)&local, (int)sizeof(local)) == SOCKET_ERROR) {
        print_wsa_error("bind(UDP IPv6)");
        close_socket(s);
        return INVALID_SOCKET;
    }

    return s;
}

static int get_local_socket_endpoint(SOCKET s, struct sockaddr_in6 *out) {
    int len = (int)sizeof(*out);
    if (getsockname(s, (struct sockaddr *)out, &len) == SOCKET_ERROR) {
        print_wsa_error("getsockname");
        return -1;
    }
    return 0;
}

static int parse_endpoint_text(const char *text, struct sockaddr_in6 *out) {
    if (!text || text[0] != '[') return -1;
    const char *rb = strchr(text, ']');
    if (!rb || rb[1] != ':') return -1;

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
    if (!end || *end != '\0' || port_ul == 0 || port_ul > 65535UL) return -1;

    memset(out, 0, sizeof(*out));
    out->sin6_family = AF_INET6;
    out->sin6_port = htons((uint16_t)port_ul);

    if (InetPtonA(AF_INET6, ip, &out->sin6_addr) != 1) return -1;
    return 0;
}

/* Route-selected source IPv6 (for fallback shareable endpoint) */
static int chosen_source_ipv6(struct in6_addr *out_addr) {
    const char *dst_ip = "2606:4700:4700::1111";
    const uint16_t dst_port = 53;

    SOCKET s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (!socket_is_valid(s)) return -1;

    struct sockaddr_in6 dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin6_family = AF_INET6;
    dst.sin6_port = htons(dst_port);
    if (InetPtonA(AF_INET6, dst_ip, &dst.sin6_addr) != 1) {
        close_socket(s);
        return -2;
    }
    if (connect(s, (struct sockaddr *)&dst, (int)sizeof(dst)) == SOCKET_ERROR) {
        close_socket(s);
        return -3;
    }

    struct sockaddr_in6 local;
    int len = (int)sizeof(local);
    if (getsockname(s, (struct sockaddr *)&local, &len) == SOCKET_ERROR) {
        close_socket(s);
        return -4;
    }

    close_socket(s);
    if (out_addr) *out_addr = local.sin6_addr;
    return 0;
}

/* ============================== STUN (same as Linux version) ============================== */

#define STUN_BINDING_REQUEST         0x0001
#define STUN_BINDING_SUCCESS         0x0101
#define STUN_MAGIC_COOKIE            0x2112A442u
#define STUN_ATTR_MAPPED_ADDRESS     0x0001
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020

static int resolve_stun_server_ipv6(const char *host, const char *port, struct sockaddr_in6 *out) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) return -1;

    int found = 0;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        if ((size_t)rp->ai_addrlen >= sizeof(struct sockaddr_in6)) {
            memcpy(out, rp->ai_addr, sizeof(struct sockaddr_in6));
            found = 1;
            break;
        }
    }
    freeaddrinfo(res);
    return found ? 0 : -2;
}

static int stun_ipv6_mapped_on_socket(SOCKET s,
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
        int sent = sendto(s, (const char *)req, (int)sizeof(req), 0,
                          (const struct sockaddr *)stun_addr, (int)sizeof(*stun_addr));
        if (sent != (int)sizeof(req)) continue;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int sel = select(0, &rfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        uint8_t resp[1500];
        struct sockaddr_in6 from;
        int from_len = (int)sizeof(from);
        int n = recvfrom(s, (char *)resp, (int)sizeof(resp), 0, (struct sockaddr *)&from, &from_len);
        if (n < 20) continue;

        uint16_t msg_type = read_be16(resp + 0);
        uint16_t msg_len  = read_be16(resp + 2);
        uint32_t cookie   = read_be32(resp + 4);

        if (cookie != STUN_MAGIC_COOKIE) continue;
        if (msg_type != STUN_BINDING_SUCCESS) continue;
        if (20 + (int)msg_len > n) continue;
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
                for (int i = 0; i < 16; i++) got_addr.s6_addr[i] = (uint8_t)(val[4 + i] ^ key[i]);
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

/* ============================== Typed packets ============================== */

/* Control packets: variable length (8-byte type prefix) */
static int send_ctrl_packet_text(SOCKET sock, const struct sockaddr_in6 *to, const char *type8, const char *payload) {
    uint8_t buf[CTRL_RX_BUFSZ];
    size_t payload_len = payload ? strlen(payload) : 0;
    size_t total = CTRL_TYPE_LEN + payload_len;
    if (total > sizeof(buf)) {
        errno = EMSGSIZE;
        return -1;
    }
    memcpy(buf, type8, CTRL_TYPE_LEN);
    if (payload_len) memcpy(buf + CTRL_TYPE_LEN, payload, payload_len);

    int sent = sendto(sock, (const char *)buf, (int)total, 0, (const struct sockaddr *)to, (int)sizeof(*to));
    if (sent != (int)total) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    return 0;
}

static int recv_ctrl_packet(SOCKET sock,
                            char out_type[CTRL_TYPE_LEN + 1],
                            uint8_t *out_payload,
                            size_t out_payload_sz,
                            size_t *out_payload_len,
                            struct sockaddr_in6 *from,
                            int *fromlen) {
    uint8_t buf[CTRL_RX_BUFSZ];
    int n = recvfrom(sock, (char *)buf, (int)sizeof(buf), 0, (struct sockaddr *)from, fromlen);
    if (n == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    if (n < CTRL_TYPE_LEN) {
        errno = EPROTO;
        return -1;
    }
    memcpy(out_type, buf, CTRL_TYPE_LEN);
    out_type[CTRL_TYPE_LEN] = '\0';

    size_t pl = (size_t)n - CTRL_TYPE_LEN;
    if (pl > out_payload_sz) pl = out_payload_sz;
    if (pl) memcpy(out_payload, buf + CTRL_TYPE_LEN, pl);
    if (out_payload_len) *out_payload_len = pl;
    return 0;
}

/* Peer packets: fixed 1200 bytes total (12-byte type prefix, payload padded with zeros) */
static int send_peer_packet_fixed(SOCKET sock,
                                  const struct sockaddr_in6 *to,
                                  const char *type12,
                                  const uint8_t *payload,
                                  size_t payload_len) {
    uint8_t buf[PEER_PKT_SIZE];
    if (payload_len > PEER_PAYLOAD_MAX) payload_len = PEER_PAYLOAD_MAX;
    memcpy(buf, type12, PEER_TYPE_LEN);
    if (payload_len) memcpy(buf + PEER_TYPE_LEN, payload, payload_len);
    if (PEER_TYPE_LEN + payload_len < PEER_PKT_SIZE) {
        memset(buf + PEER_TYPE_LEN + payload_len, 0, PEER_PKT_SIZE - (PEER_TYPE_LEN + payload_len));
    }

    int sent = sendto(sock, (const char *)buf, PEER_PKT_SIZE, 0, (const struct sockaddr *)to, (int)sizeof(*to));
    if (sent != PEER_PKT_SIZE) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    return 0;
}

static int recv_peer_packet_fixed(SOCKET sock,
                                  char out_type[PEER_TYPE_LEN + 1],
                                  uint8_t out_payload[PEER_PAYLOAD_MAX],
                                  struct sockaddr_in6 *from,
                                  int *fromlen) {
    uint8_t buf[PEER_PKT_SIZE];
    int n = recvfrom(sock, (char *)buf, (int)sizeof(buf), 0, (struct sockaddr *)from, fromlen);
    if (n == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    if (n < PEER_TYPE_LEN) {
        errno = EPROTO;
        return -1;
    }
    memcpy(out_type, buf, PEER_TYPE_LEN);
    out_type[PEER_TYPE_LEN] = '\0';

    /* payload is padded; always copy full payload max (1188) from buffer if present */
    size_t have = (size_t)n - PEER_TYPE_LEN;
    if (have > PEER_PAYLOAD_MAX) have = PEER_PAYLOAD_MAX;
    memset(out_payload, 0, PEER_PAYLOAD_MAX);
    if (have) memcpy(out_payload, buf + PEER_TYPE_LEN, have);
    return 0;
}

/* ============================== Python notifications ============================== */

static void notify_python(app_state_t *st, const char *type8, const char *text) {
    if (send_ctrl_packet_text(st->control_sock, &st->python_addr, type8, text ? text : "") != 0) {
        print_wsa_error("sendto(Python control packet)");
    }
}
static void notify_info(app_state_t *st, const char *text) {
    notify_python(st, PKT_INFO, text);
}

/* ============================== Latest message store ============================== */

static void store_latest_message(app_state_t *st, const uint8_t *payload, size_t payload_len) {
    EnterCriticalSection(&st->msg_mtx);
    size_t n = payload_len;
    if (n >= sizeof(st->latest_message)) n = sizeof(st->latest_message) - 1;
    memcpy(st->latest_message, payload, n);
    st->latest_message[n] = '\0';
    st->latest_message_ready = 1;
    LeaveCriticalSection(&st->msg_mtx);
}

/* ============================== Work queues ============================== */

static void fileq_push_locked(app_state_t *st, file_task_t *n) {
    n->prev = st->fileq_tail;
    n->next = NULL;
    if (st->fileq_tail) st->fileq_tail->next = n;
    else st->fileq_head = n;
    st->fileq_tail = n;
}

static void fileq_push(app_state_t *st, const char *path) {
    if (!path || !path[0]) return;
    file_task_t *n = (file_task_t *)calloc(1, sizeof(*n));
    if (!n) return;
    n->path = _strdup(path);
    if (!n->path) { free(n); return; }

    EnterCriticalSection(&st->work_mtx);
    fileq_push_locked(st, n);
    WakeConditionVariable(&st->work_cv);
    LeaveCriticalSection(&st->work_mtx);
}

static char *fileq_pop(app_state_t *st) {
    EnterCriticalSection(&st->work_mtx);
    file_task_t *n = st->fileq_head;
    if (!n) { LeaveCriticalSection(&st->work_mtx); return NULL; }
    st->fileq_head = n->next;
    if (st->fileq_head) st->fileq_head->prev = NULL;
    else st->fileq_tail = NULL;
    LeaveCriticalSection(&st->work_mtx);

    char *p = n->path;
    free(n);
    return p;
}

static void sendq_push_front_locked(app_state_t *st, send_node_t *n) {
    n->prev = NULL;
    n->next = st->sendq_head;
    if (st->sendq_head) st->sendq_head->prev = n;
    else st->sendq_tail = n;
    st->sendq_head = n;
}

static void sendq_push_front(app_state_t *st, const char *type12, const uint8_t *payload, size_t payload_len) {
    send_node_t *n = (send_node_t *)calloc(1, sizeof(*n));
    if (!n) return;
    memcpy(n->type, type12, PEER_TYPE_LEN);
    n->type[PEER_TYPE_LEN] = '\0';
    if (payload_len > PEER_PAYLOAD_MAX) payload_len = PEER_PAYLOAD_MAX;
    n->payload_len = payload_len;
    n->payload = (uint8_t *)malloc(payload_len ? payload_len : 1);
    if (!n->payload) { free(n); return; }
    if (payload_len && payload) memcpy(n->payload, payload, payload_len);

    EnterCriticalSection(&st->work_mtx);
    sendq_push_front_locked(st, n);
    WakeConditionVariable(&st->work_cv);
    LeaveCriticalSection(&st->work_mtx);
}

static send_node_t *sendq_pop_front_locked(app_state_t *st) {
    send_node_t *n = st->sendq_head;
    if (!n) return NULL;
    st->sendq_head = n->next;
    if (st->sendq_head) st->sendq_head->prev = NULL;
    else st->sendq_tail = NULL;
    n->prev = n->next = NULL;
    return n;
}

static int work_has_any_locked(app_state_t *st) {
    if (st->sendq_head) return 1;
    if (st->fileq_head) return 1;
    if (st->outgoing.active && st->outgoing.rq_head) return 1;
    if (st->outgoing.active && !st->outgoing.done_seen && !st->outgoing.sent_all) return 1;
    return 0;
}

static void free_send_node(send_node_t *n) {
    if (!n) return;
    free(n->payload);
    free(n);
}

static void drain_sendq(app_state_t *st);

/* ============================== Peer send wrapper ============================== */

static int peer_send_fixed(app_state_t *st, const char *type12, const uint8_t *payload, size_t payload_len) {
    SOCKET sock;
    struct sockaddr_in6 to;
    int ready;
    EnterCriticalSection(&st->peer_mtx);
    sock = st->peer_sock;
    to = st->remote_peer;
    ready = (st->peer_socket_ready && st->remote_peer_ready && socket_is_valid(sock));
    LeaveCriticalSection(&st->peer_mtx);
    if (!ready) return -1;
    return send_peer_packet_fixed(sock, &to, type12, payload, payload_len);
}

static int peer_is_ready(app_state_t *st) {
    int ready;
    EnterCriticalSection(&st->peer_mtx);
    ready = (st->peer_socket_ready && st->remote_peer_ready && socket_is_valid(st->peer_sock));
    LeaveCriticalSection(&st->peer_mtx);
    return ready;
}

/* Drain priority send queue (chat + small control-ish peer packets) */
static void drain_sendq(app_state_t *st) {
    for (;;) {
        EnterCriticalSection(&st->work_mtx);
        send_node_t *n = sendq_pop_front_locked(st);
        LeaveCriticalSection(&st->work_mtx);
        if (!n) break;

        int rc = peer_send_fixed(st, n->type, n->payload, n->payload_len);
        if (rc != 0) {
            /* peer not ready: put it back at front and stop */
            EnterCriticalSection(&st->work_mtx);
            sendq_push_front_locked(st, n);
            LeaveCriticalSection(&st->work_mtx);
            break;
        }
        free_send_node(n);
    }
}

/* ============================== Resend queue (requested indices) ============================== */

static void resendq_clear(resend_node_t **h, resend_node_t **t) {
    resend_node_t *cur = *h;
    while (cur) {
        resend_node_t *nx = cur->next;
        free(cur);
        cur = nx;
    }
    *h = *t = NULL;
}

static void resendq_push_back(outgoing_transfer_t *out, uint32_t idx) {
    resend_node_t *n = (resend_node_t *)calloc(1, sizeof(*n));
    if (!n) return;
    n->chunk_index = idx;
    n->prev = out->rq_tail;
    n->next = NULL;
    if (out->rq_tail) out->rq_tail->next = n;
    else out->rq_head = n;
    out->rq_tail = n;
}

static int resendq_pop_front(outgoing_transfer_t *out, uint32_t *out_idx) {
    resend_node_t *n = out->rq_head;
    if (!n) return 0;
    out->rq_head = n->next;
    if (out->rq_head) out->rq_head->prev = NULL;
    else out->rq_tail = NULL;
    if (out_idx) *out_idx = n->chunk_index;
    free(n);
    return 1;
}

/* ============================== File helpers ============================== */

static int ensure_received_dir(void) {
    DWORD attr = GetFileAttributesA(RECEIVED_DIR_NAME);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        if (attr & FILE_ATTRIBUTE_DIRECTORY) return 0;
        return -1;
    }
    if (!CreateDirectoryA(RECEIVED_DIR_NAME, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS) return 0;
        return -2;
    }
    return 0;
}

static int file_exists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES);
}

static int make_unique_output_path(const char *dir, const char *filename, char *out, size_t out_sz) {
    if (_snprintf(out, (int)out_sz, "%s\\%s", dir, filename) < 0) return -1;
    if (!file_exists(out)) return 0;

    const char *dot = strrchr(filename, '.');
    char base[512];
    char ext[128];
    if (dot && dot != filename) {
        size_t bl = (size_t)(dot - filename);
        if (bl >= sizeof(base)) bl = sizeof(base) - 1;
        memcpy(base, filename, bl);
        base[bl] = '\0';
        strncpy(ext, dot, sizeof(ext) - 1);
        ext[sizeof(ext) - 1] = '\0';
    } else {
        strncpy(base, filename, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        ext[0] = '\0';
    }

    for (int i = 1; i < 100000; i++) {
        if (_snprintf(out, (int)out_sz, "%s\\%s (%d)%s", dir, base, i, ext) < 0) return -2;
        if (!file_exists(out)) return 0;
    }
    return -3;
}

static const char *path_basename_const(const char *path) {
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    const char *slash = s1;
    if (s2 && (!slash || s2 > slash)) slash = s2;
    return slash ? slash + 1 : path;
}

/* ============================== Outgoing transfer ============================== */

static void outgoing_clear_locked(outgoing_transfer_t *out) {
    if (out->hfile && out->hfile != INVALID_HANDLE_VALUE) CloseHandle(out->hfile);
    out->hfile = INVALID_HANDLE_VALUE;
    resendq_clear(&out->rq_head, &out->rq_tail);
    memset(out, 0, sizeof(*out));
    out->hfile = INVALID_HANDLE_VALUE;
}

static int outgoing_build_from_file(app_state_t *st, const char *path) {
    struct _stat64 stbuf;
    if (_stat64(path, &stbuf) != 0) {
        notify_info(st, "File does not exist.");
        return -1;
    }
    if (!(stbuf.st_mode & _S_IFREG)) {
        notify_info(st, "File path is not a regular file.");
        return -2;
    }

    HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        notify_info(st, "Failed to open the requested file.");
        return -3;
    }

    const char *filename = path_basename_const(path);
    size_t filename_len = strlen(filename);
    if (filename_len == 0 || filename_len >= sizeof(st->outgoing.filename)) {
        CloseHandle(hf);
        notify_info(st, "Filename invalid or too long.");
        return -4;
    }

    uint64_t filesize = (uint64_t)stbuf.st_size;
    uint32_t total_chunks = (uint32_t)((filesize + (uint64_t)FILE_CHUNK_DATA_MAX - 1ULL) / (uint64_t)FILE_CHUNK_DATA_MAX);

    uint8_t transfer_id[TRANSFER_ID_LEN];
    (void)random_bytes(transfer_id, sizeof(transfer_id));

    EnterCriticalSection(&st->out_mtx);
    outgoing_clear_locked(&st->outgoing);

    outgoing_transfer_t *out = &st->outgoing;
    out->active = 1;
    out->hfile = hf;
    memcpy(out->transfer_id, transfer_id, TRANSFER_ID_LEN);
    out->filesize = filesize;
    out->total_chunks = total_chunks;
    strncpy(out->filename, filename, sizeof(out->filename) - 1);
    out->filename[sizeof(out->filename) - 1] = '\0';

    /* FILEMETA payload: transfer_id[16] | filesize[8] | total_chunks[4] | name_len[2] | name */
    size_t off = 0;
    memcpy(out->meta_payload + off, transfer_id, TRANSFER_ID_LEN); off += TRANSFER_ID_LEN;
    write_be64(out->meta_payload + off, filesize); off += 8;
    write_be32(out->meta_payload + off, total_chunks); off += 4;
    write_be16(out->meta_payload + off, (uint16_t)filename_len); off += 2;
    memcpy(out->meta_payload + off, filename, filename_len); off += filename_len;
    out->meta_len = off;

    out->next_index = 0;
    out->sent_all = 0;
    out->done_seen = 0;
    out->last_meta_send_ms = 0;

    out->burst = SEND_BURST_START;
    out->backoff_us = BACKOFF_US_MIN;

    LeaveCriticalSection(&st->out_mtx);
    return 0;
}

/* Read file at offset (synchronous) */
static int file_read_at(HANDLE hf, uint64_t off, uint8_t *buf, DWORD want, DWORD *got) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(hf, li, NULL, FILE_BEGIN)) return -1;
    DWORD r = 0;
    if (!ReadFile(hf, buf, want, &r, NULL)) return -1;
    if (got) *got = r;
    return 0;
}

/* Build and send one chunk (idx) by reading from disk */
static int outgoing_send_chunk(app_state_t *st, outgoing_transfer_t *out, uint32_t idx) {
    uint8_t payload[PEER_PAYLOAD_MAX];
    size_t off = 0;

    memcpy(payload + off, out->transfer_id, TRANSFER_ID_LEN); off += TRANSFER_ID_LEN;
    write_be32(payload + off, idx); off += 4;
    write_be32(payload + off, out->total_chunks); off += 4;

    uint64_t file_off = (uint64_t)idx * (uint64_t)FILE_CHUNK_DATA_MAX;
    size_t want = FILE_CHUNK_DATA_MAX;
    if (file_off + want > out->filesize) {
        if (file_off >= out->filesize) want = 0;
        else want = (size_t)(out->filesize - file_off);
    }

    uint16_t data_len = 0;
    if (want > 0) {
        DWORD got = 0;
        if (file_read_at(out->hfile, file_off, payload + off + 2, (DWORD)want, &got) != 0) {
            return -1;
        }
        data_len = (uint16_t)got;
    }
    write_be16(payload + off, data_len); off += 2;
    off += (size_t)data_len;

    return peer_send_fixed(st, PEER_FILECHNK, payload, off);
}

static int outgoing_send_meta_if_needed(app_state_t *st, outgoing_transfer_t *out, long long now) {
    if (!out->active || out->done_seen) return 0;
    if (out->last_meta_send_ms != 0 && (now - out->last_meta_send_ms) < META_RESEND_INTERVAL_MS) return 0;
    out->last_meta_send_ms = now;
    return peer_send_fixed(st, PEER_FILEMETA, out->meta_payload, out->meta_len);
}

static void sender_adjust_after_send(outgoing_transfer_t *out, int rc_ok) {
    if (rc_ok == 0) {
        if (out->burst < SEND_BURST_MAX) out->burst = out->burst + (out->burst / 8) + 1;
        if (out->burst > SEND_BURST_MAX) out->burst = SEND_BURST_MAX;
        if (out->backoff_us > BACKOFF_US_MIN) out->backoff_us -= 250;
        if (out->backoff_us < BACKOFF_US_MIN) out->backoff_us = BACKOFF_US_MIN;
    } else {
        if (out->burst > SEND_BURST_MIN) out->burst = out->burst / 2;
        if (out->burst < SEND_BURST_MIN) out->burst = SEND_BURST_MIN;
        out->backoff_us += 250;
        if (out->backoff_us > BACKOFF_US_MAX) out->backoff_us = BACKOFF_US_MAX;
    }
}

static void sender_backoff(outgoing_transfer_t *out) {
    sleep_us(out->backoff_us);
}

static int sender_send_one_resend(app_state_t *st, outgoing_transfer_t *out) {
    uint32_t idx = 0;

    EnterCriticalSection(&st->out_mtx);
    if (!out->active || out->done_seen) { LeaveCriticalSection(&st->out_mtx); return 0; }
    int has = resendq_pop_front(out, &idx);
    LeaveCriticalSection(&st->out_mtx);
    if (!has) return 0;

    int rc = outgoing_send_chunk(st, out, idx);
    if (rc != 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) {
        sender_adjust_after_send(out, -1);
        sender_backoff(out);
    } else {
        sender_adjust_after_send(out, rc);
    }
    return 1;
}

static void sender_wait_for_peer_ready(app_state_t *st) {
    while (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        if (peer_is_ready(st)) return;
        Sleep(100);
    }
}

/* Perform ONE "round" of sending for the current transfer.
   This function does NOT block for the full transfer, so it can be used in
   single-threaded fallback mode without freezing the control loop. */
static void sender_send_current_transfer_step(app_state_t *st) {
    if (InterlockedCompareExchange(&st->stop_flag, 0, 0) != 0) return;

    drain_sendq(st);

    EnterCriticalSection(&st->out_mtx);
    outgoing_transfer_t *out = &st->outgoing;
    int active = out->active;
    int done = out->done_seen;
    long long now = now_ms();
    LeaveCriticalSection(&st->out_mtx);

    if (!active || done) return;

    /* resend meta periodically */
    EnterCriticalSection(&st->out_mtx);
    (void)outgoing_send_meta_if_needed(st, &st->outgoing, now);
    LeaveCriticalSection(&st->out_mtx);

    /* prioritize resend requests (small batch) */
    for (int i = 0; i < 16; i++) {
        int r = sender_send_one_resend(st, &st->outgoing);
        if (r <= 0) break;
    }

    /* send forward chunks in a dynamic burst */
    EnterCriticalSection(&st->out_mtx);
    out = &st->outgoing;
    if (!out->active || out->done_seen) { LeaveCriticalSection(&st->out_mtx); return; }
    int burst = out->burst;
    LeaveCriticalSection(&st->out_mtx);

    int sent_this_round = 0;
    while (sent_this_round < burst) {
        if (InterlockedCompareExchange(&st->stop_flag, 0, 0) != 0) return;
        drain_sendq(st);

        uint32_t idx;
        EnterCriticalSection(&st->out_mtx);
        out = &st->outgoing;
        if (!out->active || out->done_seen) { LeaveCriticalSection(&st->out_mtx); break; }
        if (out->next_index >= out->total_chunks) {
            out->sent_all = 1;
            LeaveCriticalSection(&st->out_mtx);
            break;
        }
        idx = out->next_index;
        out->next_index++;
        LeaveCriticalSection(&st->out_mtx);

        int rc = outgoing_send_chunk(st, &st->outgoing, idx);
        if (rc != 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) {
            sender_adjust_after_send(&st->outgoing, -1);
            sender_backoff(&st->outgoing);
            break;
        } else {
            sender_adjust_after_send(&st->outgoing, rc);
        }
        sent_this_round++;
    }
}

/* Blocking loop used by the sender thread: keeps calling the step function until done. */
static void sender_send_current_transfer(app_state_t *st) {
    for (;;) {
        if (InterlockedCompareExchange(&st->stop_flag, 0, 0) != 0) return;

        EnterCriticalSection(&st->out_mtx);
        int active = st->outgoing.active;
        int done = st->outgoing.done_seen;
        int sent_all = st->outgoing.sent_all;
        LeaveCriticalSection(&st->out_mtx);

        if (!active || done) return;

        sender_send_current_transfer_step(st);

        /* If everything has been sent forward already, avoid busy-spinning while waiting for confirmation. */
        if (sent_all) Sleep(10);
    }
}

/* ============================== Incoming transfer (bitmap + SetFilePointerEx/WriteFile) ============================== */
/* ============================== (bitmap + SetFilePointerEx/WriteFile) ============================== */

static void incoming_clear_locked(incoming_transfer_t *in) {
    if (in->hfile && in->hfile != INVALID_HANDLE_VALUE) CloseHandle(in->hfile);
    in->hfile = INVALID_HANDLE_VALUE;
    bitmap_free(&in->received_bm);
    memset(in, 0, sizeof(*in));
    in->hfile = INVALID_HANDLE_VALUE;
}

/* Write file at offset (synchronous) */
static int file_write_at(HANDLE hf, uint64_t off, const uint8_t *buf, DWORD len) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(hf, li, NULL, FILE_BEGIN)) return -1;
    DWORD wrote = 0;
    if (!WriteFile(hf, buf, len, &wrote, NULL)) return -1;
    return (wrote == len) ? 0 : -1;
}

static int incoming_handle_meta(app_state_t *st, const uint8_t *payload) {
    /* transfer_id[16] | filesize[8] | total[4] | name_len[2] | name */
    const size_t need_min = TRANSFER_ID_LEN + 8 + 4 + 2;
    if (PEER_PAYLOAD_MAX < need_min) return -1;

    const uint8_t *p = payload;

    uint8_t tid[TRANSFER_ID_LEN];
    memcpy(tid, p, TRANSFER_ID_LEN); p += TRANSFER_ID_LEN;
    uint64_t filesize = read_be64(p); p += 8;
    uint32_t total = read_be32(p); p += 4;
    uint16_t name_len = read_be16(p); p += 2;

    if (name_len == 0 || name_len >= 512) return -2;
    if (need_min + name_len > PEER_PAYLOAD_MAX) return -3;

    EnterCriticalSection(&st->in_mtx);
    incoming_transfer_t *in = &st->incoming;

    /* ignore duplicate meta for same transfer */
    if (in->active && memcmp(in->transfer_id, tid, TRANSFER_ID_LEN) == 0) {
        LeaveCriticalSection(&st->in_mtx);
        return 0;
    }

    incoming_clear_locked(in);

    if (ensure_received_dir() != 0) {
        LeaveCriticalSection(&st->in_mtx);
        notify_info(st, "Failed to create ReceivedFiles directory.");
        return -4;
    }

    memcpy(in->transfer_id, tid, TRANSFER_ID_LEN);
    in->filesize = filesize;
    in->total_chunks = total;
    in->received_chunks = 0;
    memcpy(in->filename, p, name_len);
    in->filename[name_len] = '\0';

    if (make_unique_output_path(RECEIVED_DIR_NAME, in->filename, in->output_path, sizeof(in->output_path)) != 0) {
        LeaveCriticalSection(&st->in_mtx);
        notify_info(st, "Failed to pick unique output filename.");
        return -5;
    }

    if (bitmap_init(&in->received_bm, total ? total : 1) != 0) {
        incoming_clear_locked(in);
        LeaveCriticalSection(&st->in_mtx);
        notify_info(st, "Failed to allocate receive bitmap.");
        return -6;
    }

    HANDLE hf = CreateFileA(in->output_path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ,
                            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        incoming_clear_locked(in);
        LeaveCriticalSection(&st->in_mtx);
        notify_info(st, "Failed to open output file for writing.");
        return -7;
    }

    /* Pre-size file (optional but helpful) */
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)filesize;
    SetFilePointerEx(hf, li, NULL, FILE_BEGIN);
    SetEndOfFile(hf);
    li.QuadPart = 0;
    SetFilePointerEx(hf, li, NULL, FILE_BEGIN);

    in->hfile = hf;
    in->active = 1;
    in->scan_pos = 0;
    in->last_missing_check_ms = 0;
    in->done_sent = 0;
    in->done_confirmed = 0;
    in->last_done_send_ms = 0;

    LeaveCriticalSection(&st->in_mtx);
    return 0;
}

static void incoming_maybe_complete(app_state_t *st, incoming_transfer_t *in) {
    if (in->received_chunks < in->total_chunks) return;

    /* Send FILEDONE */
    uint8_t done_payload[TRANSFER_ID_LEN + 4];
    memcpy(done_payload, in->transfer_id, TRANSFER_ID_LEN);
    write_be32(done_payload + TRANSFER_ID_LEN, in->total_chunks);
    (void)peer_send_fixed(st, PEER_FILEDONE, done_payload, sizeof(done_payload));

    in->done_sent = 1;
    in->done_confirmed = 0;
    in->last_done_send_ms = now_ms();

    /* Close output file handle (file already assembled in-place) */
    if (in->hfile && in->hfile != INVALID_HANDLE_VALUE) {
        CloseHandle(in->hfile);
        in->hfile = INVALID_HANDLE_VALUE;
    }

    char info[PATH_MAX_CHARS + 64];
    _snprintf(info, sizeof(info), "File received: %s", in->output_path);
    notify_info(st, info);

    /* mark inactive but keep transfer id to allow DONE resend until CONF arrives */
    in->active = 0;
    bitmap_free(&in->received_bm);
}

static int incoming_handle_chunk(app_state_t *st, const uint8_t *payload) {
    /* transfer_id[16] | idx[4] | total[4] | data_len[2] | data */
    const uint8_t *p = payload;
    uint8_t tid[TRANSFER_ID_LEN];
    memcpy(tid, p, TRANSFER_ID_LEN); p += TRANSFER_ID_LEN;
    uint32_t idx = read_be32(p); p += 4;
    uint32_t total = read_be32(p); p += 4;
    uint16_t data_len = read_be16(p); p += 2;

    if ((size_t)data_len > FILE_CHUNK_DATA_MAX) return -1;
    if ((size_t)(p - payload) + (size_t)data_len > PEER_PAYLOAD_MAX) return -2;

    EnterCriticalSection(&st->in_mtx);
    incoming_transfer_t *in = &st->incoming;

    if (!in->active || memcmp(in->transfer_id, tid, TRANSFER_ID_LEN) != 0) {
        LeaveCriticalSection(&st->in_mtx);
        return -3;
    }
    if (in->total_chunks != total || idx >= total) {
        LeaveCriticalSection(&st->in_mtx);
        return -4;
    }

    if (bitmap_test(&in->received_bm, idx)) {
        LeaveCriticalSection(&st->in_mtx);
        return 0;
    }

    uint64_t file_off = (uint64_t)idx * (uint64_t)FILE_CHUNK_DATA_MAX;

    /* write chunk */
    if (data_len > 0) {
        if (file_write_at(in->hfile, file_off, p, (DWORD)data_len) != 0) {
            LeaveCriticalSection(&st->in_mtx);
            return -5;
        }
    }

    bitmap_set(&in->received_bm, idx);
    in->received_chunks++;

    incoming_maybe_complete(st, in);

    LeaveCriticalSection(&st->in_mtx);
    return 0;
}

static void receiver_request_missing(app_state_t *st, long long now) {
    uint8_t transfer_id[TRANSFER_ID_LEN];
    uint32_t total = 0;
    bitmap_t *bm = NULL;
    uint32_t scan_pos = 0;
    int do_request = 0;

    EnterCriticalSection(&st->in_mtx);
    incoming_transfer_t *in = &st->incoming;
    if (in->active && in->received_bm.words && in->total_chunks > 0) {
        if (in->last_missing_check_ms == 0 || (now - in->last_missing_check_ms) >= MISSING_CHECK_INTERVAL_MS) {
            memcpy(transfer_id, in->transfer_id, TRANSFER_ID_LEN);
            total = in->total_chunks;
            bm = &in->received_bm;
            scan_pos = in->scan_pos;
            in->last_missing_check_ms = now;
            do_request = 1;
        }
    }
    LeaveCriticalSection(&st->in_mtx);

    if (!do_request || !bm) return;

    uint32_t missing[MAX_REQ_BATCH];
    uint16_t count = 0;
    uint32_t steps = 0;
    uint32_t pos = scan_pos;

    while (count < MAX_REQ_BATCH && steps < MAX_MISSING_SCAN_STEPS && steps < total) {
        uint32_t idx = pos;
        if (!bitmap_test(bm, idx)) missing[count++] = idx;
        pos++;
        if (pos >= total) pos = 0;
        steps++;
    }

    EnterCriticalSection(&st->in_mtx);
    st->incoming.scan_pos = pos;
    LeaveCriticalSection(&st->in_mtx);

    if (count == 0) return;

    size_t need = TRANSFER_ID_LEN + 2 + (size_t)count * 4;
    if (need > PEER_PAYLOAD_MAX) {
        count = (uint16_t)((PEER_PAYLOAD_MAX - (TRANSFER_ID_LEN + 2)) / 4);
        need = TRANSFER_ID_LEN + 2 + (size_t)count * 4;
    }

    uint8_t payload[PEER_PAYLOAD_MAX];
    size_t off = 0;
    memcpy(payload + off, transfer_id, TRANSFER_ID_LEN); off += TRANSFER_ID_LEN;
    write_be16(payload + off, count); off += 2;
    for (uint16_t i = 0; i < count; i++) {
        write_be32(payload + off, missing[i]);
        off += 4;
    }
    (void)peer_send_fixed(st, PEER_FILEREQ, payload, off);
}

static void receiver_resend_done_if_needed(app_state_t *st, long long now) {
    uint8_t transfer_id[TRANSFER_ID_LEN];
    uint32_t total = 0;
    int do_resend = 0;

    EnterCriticalSection(&st->in_mtx);
    incoming_transfer_t *in = &st->incoming;
    if (in->done_sent && !in->done_confirmed) {
        if (in->last_done_send_ms == 0 || (now - in->last_done_send_ms) >= DONE_RESEND_INTERVAL_MS) {
            memcpy(transfer_id, in->transfer_id, TRANSFER_ID_LEN);
            total = in->total_chunks;
            in->last_done_send_ms = now;
            do_resend = 1;
        }
    }
    LeaveCriticalSection(&st->in_mtx);

    if (!do_resend) return;
    uint8_t payload[TRANSFER_ID_LEN + 4];
    memcpy(payload, transfer_id, TRANSFER_ID_LEN);
    write_be32(payload + TRANSFER_ID_LEN, total);
    (void)peer_send_fixed(st, PEER_FILEDONE, payload, sizeof(payload));
}

/* ============================== Peer packet handling ============================== */

static void handle_peer_packet(app_state_t *st, const char type[PEER_TYPE_LEN + 1], const uint8_t payload[PEER_PAYLOAD_MAX]) {
    if (strcmp(type, PEER_MSG) == 0) {
        /* Payload format: u16 msg_len | msg bytes
           Backwards compatibility: if invalid, treat as C-string. */
        if (PEER_PAYLOAD_MAX >= 2) {
            uint16_t ml = read_be16(payload);
            if (ml <= (uint16_t)(PEER_PAYLOAD_MAX - 2)) {
                store_latest_message(st, payload + 2, ml);
                return;
            }
        }
        /* fallback: treat as string trimmed at first NUL */
        size_t n = strnlen_portable((const char *)payload, PEER_PAYLOAD_MAX);
        store_latest_message(st, payload, n);
        return;
    }

    if (strcmp(type, PEER_EXIT) == 0) {
        notify_info(st, "The remote peer ended the session. Closing this bridge.");
        InterlockedExchange(&st->stop_flag, 1);
        WakeAllConditionVariable(&st->work_cv);
        WakeAllConditionVariable(&st->out_cv);
        return;
    }

    if (strcmp(type, PEER_PING) == 0) return;

    if (strcmp(type, PEER_FILEMETA) == 0) {
        (void)incoming_handle_meta(st, payload);
        return;
    }

    if (strcmp(type, PEER_FILECHNK) == 0) {
        (void)incoming_handle_chunk(st, payload);
        return;
    }

    if (strcmp(type, PEER_FILEREQ) == 0) {
        if (PEER_PAYLOAD_MAX < TRANSFER_ID_LEN + 2) return;
        const uint8_t *p = payload;
        uint8_t tid[TRANSFER_ID_LEN];
        memcpy(tid, p, TRANSFER_ID_LEN); p += TRANSFER_ID_LEN;
        uint16_t count = read_be16(p); p += 2;
        if ((size_t)(TRANSFER_ID_LEN + 2 + (size_t)count * 4) > PEER_PAYLOAD_MAX) return;

        EnterCriticalSection(&st->out_mtx);
        outgoing_transfer_t *out = &st->outgoing;
        if (out->active && memcmp(out->transfer_id, tid, TRANSFER_ID_LEN) == 0) {
            for (uint16_t i = 0; i < count; i++) {
                uint32_t idx = read_be32(p); p += 4;
                if (idx < out->total_chunks) resendq_push_back(out, idx);
            }
            WakeConditionVariable(&st->work_cv);
        }
        LeaveCriticalSection(&st->out_mtx);
        return;
    }

    if (strcmp(type, PEER_FILEDONE) == 0) {
        /* receiver says done -> sender acks with FILECONF and marks done */
        if (PEER_PAYLOAD_MAX < TRANSFER_ID_LEN + 4) return;
        uint8_t tid[TRANSFER_ID_LEN];
        memcpy(tid, payload, TRANSFER_ID_LEN);

        uint8_t conf[TRANSFER_ID_LEN];
        memcpy(conf, tid, TRANSFER_ID_LEN);
        sendq_push_front(st, PEER_FILECONF, conf, sizeof(conf)); /* priority */

        EnterCriticalSection(&st->out_mtx);
        outgoing_transfer_t *out = &st->outgoing;
        if (out->active && memcmp(out->transfer_id, tid, TRANSFER_ID_LEN) == 0) {
            out->done_seen = 1;
            WakeAllConditionVariable(&st->out_cv);
            WakeConditionVariable(&st->work_cv);
        }
        LeaveCriticalSection(&st->out_mtx);
        return;
    }

    if (strcmp(type, PEER_FILECONF) == 0) {
        if (PEER_PAYLOAD_MAX < TRANSFER_ID_LEN) return;
        EnterCriticalSection(&st->in_mtx);
        incoming_transfer_t *in = &st->incoming;
        if (in->done_sent && memcmp(in->transfer_id, payload, TRANSFER_ID_LEN) == 0) {
            in->done_confirmed = 1;
            memset(in->transfer_id, 0, TRANSFER_ID_LEN);
            in->done_sent = 0;
        }
        LeaveCriticalSection(&st->in_mtx);
        return;
    }
}

/* ============================== Keepalive / punch ============================== */

static void schedule_initial_punches(app_state_t *st) {
    st->punches_left = INITIAL_PUNCH_COUNT;
    st->last_punch_ms = 0;
    st->last_keepalive_ms = now_ms();
}

static void maybe_send_periodic_peer_packets(app_state_t *st) {
    if (!peer_is_ready(st)) return;
    long long now = now_ms();

    if (st->punches_left > 0) {
        if (st->last_punch_ms == 0 || (now - st->last_punch_ms) >= INITIAL_PUNCH_INTERVAL_MS) {
            (void)peer_send_fixed(st, PEER_PING, (const uint8_t *)"hello", 5);
            st->last_punch_ms = now;
            st->punches_left--;
        }
    }

    if ((now - st->last_keepalive_ms) >= KEEPALIVE_INTERVAL_MS) {
        (void)peer_send_fixed(st, PEER_PING, (const uint8_t *)"keepalive", 9);
        st->last_keepalive_ms = now;
    }
}

/* ============================== Shareable endpoint ============================== */

static int build_shareable_endpoint_text(app_state_t *st, char *out, size_t out_sz) {
    struct sockaddr_in6 bound_local;
    EnterCriticalSection(&st->peer_mtx);
    SOCKET sock = st->peer_sock;
    peer_mode_t mode = st->mode;
    LeaveCriticalSection(&st->peer_mtx);

    if (!socket_is_valid(sock)) return -1;
    if (get_local_socket_endpoint(sock, &bound_local) != 0) return -1;

    if (mode == MODE_LOCAL) {
        format_sockaddr6_full(&bound_local, out, out_sz);
        return 0;
    }

    struct sockaddr_in6 stun_addr;
    if (resolve_stun_server_ipv6("stun.cloudflare.com", "3478", &stun_addr) == 0) {
        struct in6_addr mapped;
        uint16_t mport = 0;
        if (stun_ipv6_mapped_on_socket(sock, &stun_addr, &mapped, &mport) == 0) {
            struct sockaddr_in6 pub;
            memset(&pub, 0, sizeof(pub));
            pub.sin6_family = AF_INET6;
            pub.sin6_addr = mapped;
            pub.sin6_port = htons(mport);
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

/* ============================== Peer socket creation ============================== */

static void close_peer_socket_if_open(app_state_t *st) {
    EnterCriticalSection(&st->peer_mtx);
    if (socket_is_valid(st->peer_sock)) {
        close_socket(st->peer_sock);
        st->peer_sock = INVALID_SOCKET;
    }
    st->peer_socket_ready = 0;
    st->remote_peer_ready = 0;
    st->mode = MODE_NONE;
    st->punches_left = 0;
    st->shareable_endpoint[0] = '\0';
    LeaveCriticalSection(&st->peer_mtx);
}

static int create_peer_socket_local(app_state_t *st) {
    close_peer_socket_if_open(st);
    SOCKET s = bind_udp_ipv6("::1", 0);
    if (!socket_is_valid(s)) return -1;

    EnterCriticalSection(&st->peer_mtx);
    st->peer_sock = s;
    st->mode = MODE_LOCAL;
    st->peer_socket_ready = 1;
    LeaveCriticalSection(&st->peer_mtx);

    if (build_shareable_endpoint_text(st, st->shareable_endpoint, sizeof(st->shareable_endpoint)) != 0) return -2;
    notify_python(st, PKT_MYENDP, st->shareable_endpoint);
    notify_info(st, "Local loopback peer socket created.");
    return 0;
}

static int create_peer_socket_public(app_state_t *st) {
    close_peer_socket_if_open(st);
    SOCKET s = bind_udp_ipv6("::", 0);
    if (!socket_is_valid(s)) return -1;

    EnterCriticalSection(&st->peer_mtx);
    st->peer_sock = s;
    st->mode = MODE_PUBLIC;
    st->peer_socket_ready = 1;
    LeaveCriticalSection(&st->peer_mtx);

    if (build_shareable_endpoint_text(st, st->shareable_endpoint, sizeof(st->shareable_endpoint)) != 0) return -2;
    notify_python(st, PKT_MYENDP, st->shareable_endpoint);
    notify_info(st, "Public peer socket created.");
    return 0;
}

/* ============================== Python control handling ============================== */

static void handle_python_control_packet(app_state_t *st,
                                         const char type[CTRL_TYPE_LEN + 1],
                                         const uint8_t *payload,
                                         size_t payload_len,
                                         int *should_exit) {
    /* payload is raw bytes, not guaranteed NUL terminated */
    char tmp[CTRL_RX_BUFSZ];
    size_t n = payload_len;
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, payload, n);
    tmp[n] = '\0';

    if (strcmp(type, PKT_MKLOCAL) == 0) {
        if (create_peer_socket_local(st) != 0) notify_info(st, "Failed to create local peer socket.");
        return;
    }
    if (strcmp(type, PKT_MKPUB) == 0) {
        if (create_peer_socket_public(st) != 0) notify_info(st, "Failed to create public peer socket.");
        return;
    }
    if (strcmp(type, PKT_SETPEER) == 0) {
        if (!st->peer_socket_ready) {
            notify_info(st, "Create the peer socket first.");
            return;
        }
        struct sockaddr_in6 peer;
        if (parse_endpoint_text(tmp, &peer) != 0) {
            notify_info(st, "Remote peer endpoint invalid. Expected [ipv6]:port.");
            return;
        }
        EnterCriticalSection(&st->peer_mtx);
        st->remote_peer = peer;
        st->remote_peer_ready = 1;
        LeaveCriticalSection(&st->peer_mtx);
        schedule_initial_punches(st);
        notify_info(st, "Remote peer endpoint saved.");
        return;
    }
    if (strcmp(type, PKT_MSG) == 0) {
        /* Queue chat message (priority) to peer, payload format u16 len | bytes */
        size_t msg_len = payload_len;
        if (msg_len > (PEER_PAYLOAD_MAX - 2)) msg_len = PEER_PAYLOAD_MAX - 2;

        uint8_t pp[PEER_PAYLOAD_MAX];
        write_be16(pp, (uint16_t)msg_len);
        if (msg_len) memcpy(pp + 2, payload, msg_len);

        sendq_push_front(st, PEER_MSG, pp, 2 + msg_len);
        notify_info(st, "Queued message (priority).");
        return;
    }
    if (strcmp(type, PKT_SNDFILE) == 0) {
        fileq_push(st, tmp);
        notify_info(st, "Queued file for sending.");
        return;
    }
    if (strcmp(type, PKT_GETMSG) == 0) {
        EnterCriticalSection(&st->msg_mtx);
        int ready = st->latest_message_ready;
        char out[MAX_MESSAGE_STORE];
        out[0] = '\0';
        if (ready) {
            strncpy(out, st->latest_message, sizeof(out) - 1);
            out[sizeof(out) - 1] = '\0';
            st->latest_message_ready = 0;
            st->latest_message[0] = '\0';
        }
        LeaveCriticalSection(&st->msg_mtx);
        if (ready) notify_python(st, PKT_MSG, out);
        else notify_info(st, "GETMSG:EMPTY");
        return;
    }
    if (strcmp(type, PKT_GETENDP) == 0) {
        if (st->shareable_endpoint[0] == '\0') notify_info(st, "No shareable endpoint is available yet.");
        else notify_python(st, PKT_MYENDP, st->shareable_endpoint);
        return;
    }
    if (strcmp(type, PKT_EXIT) == 0) {
        /* best-effort tell peer */
        sendq_push_front(st, PEER_EXIT, NULL, 0);
        notify_info(st, "Local user requested exit. Closing this bridge.");
        *should_exit = 1;
        return;
    }

    notify_info(st, "Unknown Python control packet type received.");
}

/* ============================== Threads ============================== */

static DWORD WINAPI sender_thread_main(LPVOID arg) {
    app_state_t *st = (app_state_t *)arg;

    while (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        EnterCriticalSection(&st->work_mtx);
        while (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0 && !work_has_any_locked(st)) {
            SleepConditionVariableCS(&st->work_cv, &st->work_mtx, INFINITE);
        }
        LeaveCriticalSection(&st->work_mtx);
        if (InterlockedCompareExchange(&st->stop_flag, 0, 0) != 0) break;

        /* priority messages */
        drain_sendq(st);

        /* if active transfer, continue it */
        EnterCriticalSection(&st->out_mtx);
        int out_active = st->outgoing.active;
        int out_done = st->outgoing.done_seen;
        LeaveCriticalSection(&st->out_mtx);

        if (out_active && !out_done) {
            sender_wait_for_peer_ready(st);
            sender_send_current_transfer(st);

            EnterCriticalSection(&st->out_mtx);
            out_done = st->outgoing.done_seen;
            if (out_done) {
                outgoing_clear_locked(&st->outgoing);
                LeaveCriticalSection(&st->out_mtx);
                notify_info(st, "Receiver confirmed file transfer. Cleared send state.");
            } else {
                LeaveCriticalSection(&st->out_mtx);
            }
            continue;
        }

        /* start next file */
        char *path = fileq_pop(st);
        if (!path) continue;

        sender_wait_for_peer_ready(st);
        if (InterlockedCompareExchange(&st->stop_flag, 0, 0) != 0) { free(path); break; }

        if (outgoing_build_from_file(st, path) != 0) {
            free(path);
            continue;
        }
        free(path);

        sender_send_current_transfer(st);

        EnterCriticalSection(&st->out_mtx);
        out_done = st->outgoing.done_seen;
        if (out_done) {
            outgoing_clear_locked(&st->outgoing);
            LeaveCriticalSection(&st->out_mtx);
            notify_info(st, "Receiver confirmed file transfer. Cleared send state.");
        } else {
            LeaveCriticalSection(&st->out_mtx);
        }
    }
    return 0;
}

static DWORD WINAPI peer_rx_thread_main(LPVOID arg) {
    app_state_t *st = (app_state_t *)arg;

    while (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        SOCKET sock = INVALID_SOCKET;
        EnterCriticalSection(&st->peer_mtx);
        if (st->peer_socket_ready) sock = st->peer_sock;
        LeaveCriticalSection(&st->peer_mtx);

        if (!socket_is_valid(sock)) {
            Sleep(100);
        } else {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000; /* 100ms */

            int sel = select(0, &rfds, NULL, NULL, &tv);
            if (sel > 0 && FD_ISSET(sock, &rfds)) {
                char type[PEER_TYPE_LEN + 1];
                uint8_t payload[PEER_PAYLOAD_MAX];
                struct sockaddr_in6 from;
                int fromlen = (int)sizeof(from);
                if (recv_peer_packet_fixed(sock, type, payload, &from, &fromlen) == 0) {
                    handle_peer_packet(st, type, payload);
                }
            }
        }

        long long now = now_ms();
        receiver_request_missing(st, now);
        receiver_resend_done_if_needed(st, now);
    }
    return 0;
}

/* ============================== Main loop ============================== */

static int run_bridge_loop(app_state_t *st) {
    int should_exit = 0;

    while (!should_exit && InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(st->control_sock, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000;

        int sel = select(0, &rfds, NULL, NULL, &tv);
        if (sel == SOCKET_ERROR) {
            print_wsa_error("select(control)");
            return 1;
        }

        if (sel > 0 && FD_ISSET(st->control_sock, &rfds)) {
            char type[CTRL_TYPE_LEN + 1];
            uint8_t payload[CTRL_RX_BUFSZ];
            size_t payload_len = 0;
            struct sockaddr_in6 from;
            int fromlen = (int)sizeof(from);
            if (recv_ctrl_packet(st->control_sock, type, payload, sizeof(payload), &payload_len, &from, &fromlen) == 0) {
                handle_python_control_packet(st, type, payload, payload_len, &should_exit);
            }
        }


        /* Single-thread fallback: handle peer I/O and sending work in the main loop. */
        if (!st->threading_enabled) {
            /* Drain messages first (priority) */
            drain_sendq(st);

            /* Poll peer socket for packets (non-blocking select with 0 timeout) */
            SOCKET ps = INVALID_SOCKET;
            EnterCriticalSection(&st->peer_mtx);
            if (st->peer_socket_ready) ps = st->peer_sock;
            LeaveCriticalSection(&st->peer_mtx);

            if (socket_is_valid(ps)) {
                fd_set prfds;
                FD_ZERO(&prfds);
                FD_SET(ps, &prfds);
                struct timeval ptv;
                ptv.tv_sec = 0;
                ptv.tv_usec = 0;
                int psel = select(0, &prfds, NULL, NULL, &ptv);
                if (psel > 0 && FD_ISSET(ps, &prfds)) {
                    char ptype[PEER_TYPE_LEN + 1];
                    uint8_t ppayload[PEER_PAYLOAD_MAX];
                    struct sockaddr_in6 pfrom;
                    int pfromlen = (int)sizeof(pfrom);
                    if (recv_peer_packet_fixed(ps, ptype, ppayload, &pfrom, &pfromlen) == 0) {
                        handle_peer_packet(st, ptype, ppayload);
                    }
                }
            }

            long long now = now_ms();
            receiver_request_missing(st, now);
            receiver_resend_done_if_needed(st, now);

            /* Clear outgoing if confirmed done */
            EnterCriticalSection(&st->out_mtx);
            int out_active = st->outgoing.active;
            int out_done = st->outgoing.done_seen;
            LeaveCriticalSection(&st->out_mtx);

            if (out_active && out_done) {
                EnterCriticalSection(&st->out_mtx);
                outgoing_clear_locked(&st->outgoing);
                LeaveCriticalSection(&st->out_mtx);
                notify_info(st, "Receiver confirmed file transfer. Cleared send state.");
            }

            /* Start a new outgoing transfer if none active */
            EnterCriticalSection(&st->out_mtx);
            out_active = st->outgoing.active;
            LeaveCriticalSection(&st->out_mtx);

            if (!out_active) {
                char *path = fileq_pop(st);
                if (path) {
                    if (peer_is_ready(st)) {
                        (void)outgoing_build_from_file(st, path);
                    }
                    free(path);
                }
            }

            /* If active, advance sending a little */
            EnterCriticalSection(&st->out_mtx);
            out_active = st->outgoing.active;
            out_done = st->outgoing.done_seen;
            LeaveCriticalSection(&st->out_mtx);
            if (out_active && !out_done) {
                sender_send_current_transfer_step(st);
            }

            /* Drain again */
            drain_sendq(st);
        }

        maybe_send_periodic_peer_packets(st);
    }

    return 0;
}

/* ============================== Init / cleanup ============================== */

static void app_state_init(app_state_t *st) {
    memset(st, 0, sizeof(*st));
    st->control_sock = INVALID_SOCKET;
    st->peer_sock = INVALID_SOCKET;
    st->mode = MODE_NONE;
    st->threading_enabled = 0;
    st->stop_flag = 0;
    st->outgoing.hfile = INVALID_HANDLE_VALUE;
    st->incoming.hfile = INVALID_HANDLE_VALUE;

    InitializeCriticalSection(&st->msg_mtx);
    InitializeCriticalSection(&st->peer_mtx);
    InitializeCriticalSection(&st->work_mtx);
    InitializeConditionVariable(&st->work_cv);
    InitializeCriticalSection(&st->out_mtx);
    InitializeConditionVariable(&st->out_cv);
    InitializeCriticalSection(&st->in_mtx);

    st->latest_message[0] = '\0';
    st->latest_message_ready = 0;
}

static void app_state_destroy(app_state_t *st) {
    /* free queues */
    EnterCriticalSection(&st->work_mtx);
    send_node_t *sc = st->sendq_head;
    st->sendq_head = st->sendq_tail = NULL;
    file_task_t *fc = st->fileq_head;
    st->fileq_head = st->fileq_tail = NULL;
    LeaveCriticalSection(&st->work_mtx);

    while (sc) { send_node_t *nx = sc->next; free_send_node(sc); sc = nx; }
    while (fc) { file_task_t *nx = fc->next; free(fc->path); free(fc); fc = nx; }

    EnterCriticalSection(&st->out_mtx);
    outgoing_clear_locked(&st->outgoing);
    LeaveCriticalSection(&st->out_mtx);

    EnterCriticalSection(&st->in_mtx);
    incoming_clear_locked(&st->incoming);
    LeaveCriticalSection(&st->in_mtx);

    close_peer_socket_if_open(st);
    close_socket(st->control_sock);

    DeleteCriticalSection(&st->msg_mtx);
    DeleteCriticalSection(&st->peer_mtx);
    DeleteCriticalSection(&st->work_mtx);
    DeleteCriticalSection(&st->out_mtx);
    DeleteCriticalSection(&st->in_mtx);
}

static int start_threads(app_state_t *st) {
    st->threading_enabled = 1;
    st->sender_thread = CreateThread(NULL, 0, sender_thread_main, st, 0, NULL);
    if (!st->sender_thread) { st->threading_enabled = 0; return -1; }
    st->peer_rx_thread = CreateThread(NULL, 0, peer_rx_thread_main, st, 0, NULL);
    if (!st->peer_rx_thread) {
        InterlockedExchange(&st->stop_flag, 1);
        WakeAllConditionVariable(&st->work_cv);
        WaitForSingleObject(st->sender_thread, INFINITE);
        CloseHandle(st->sender_thread);
        st->threading_enabled = 0;
        InterlockedExchange(&st->stop_flag, 0);
        return -2;
    }
    return 0;
}

static void stop_threads(app_state_t *st) {
    InterlockedExchange(&st->stop_flag, 1);
    WakeAllConditionVariable(&st->work_cv);
    WakeAllConditionVariable(&st->out_cv);

    if (st->threading_enabled) {
        if (st->sender_thread) {
            WaitForSingleObject(st->sender_thread, INFINITE);
            CloseHandle(st->sender_thread);
            st->sender_thread = NULL;
        }
        if (st->peer_rx_thread) {
            WaitForSingleObject(st->peer_rx_thread, INFINITE);
            CloseHandle(st->peer_rx_thread);
            st->peer_rx_thread = NULL;
        }
    }
}

/* ============================== main ============================== */

int main(int argc, char **argv) {
    if (sockets_init() != 0) return 1;

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
    app_state_init(&st);

    memset(&st.python_addr, 0, sizeof(st.python_addr));
    st.python_addr.sin6_family = AF_INET6;
    st.python_addr.sin6_port = htons((uint16_t)py_port_ul);
    if (InetPtonA(AF_INET6, "::1", &st.python_addr.sin6_addr) != 1) {
        fprintf(stderr, "Failed to build Python address ::1\n");
        app_state_destroy(&st);
        sockets_cleanup();
        return 1;
    }

    st.control_sock = bind_udp_ipv6("::1", 0);
    if (!socket_is_valid(st.control_sock)) {
        app_state_destroy(&st);
        sockets_cleanup();
        return 1;
    }

    struct sockaddr_in6 control_local;
    if (get_local_socket_endpoint(st.control_sock, &control_local) != 0) {
        app_state_destroy(&st);
        sockets_cleanup();
        return 1;
    }

    char ctl_port_text[32];
    _snprintf(ctl_port_text, sizeof(ctl_port_text), "%u", ntohs(control_local.sin6_port));
    notify_python(&st, PKT_CTLPORT, ctl_port_text);
    notify_info(&st, "Bridge started. Waiting for Python commands.");

    if (start_threads(&st) != 0) {
        st.threading_enabled = 0;
        notify_info(&st, "Thread start failed; using single-threaded mode.");
    }

    int rc = run_bridge_loop(&st);

    stop_threads(&st);
    app_state_destroy(&st);
    sockets_cleanup();
    return rc;
}
