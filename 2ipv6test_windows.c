#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <bcrypt.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

typedef SOCKET sock_t;
typedef int socklen_t;

#ifndef F_OK
#define F_OK 0
#endif

static void perror_sock(const char *msg) {
    int e = WSAGetLastError();
    fprintf(stderr, "%s: WSA error %d\n", msg, e);
}

#define SOCK_INVALID INVALID_SOCKET
#define close_sock(s) closesocket(s)

static void sleep_us(unsigned int us) { Sleep((us + 999U) / 1000U); }
static void sleep_ms(unsigned int ms) { Sleep(ms); }

typedef struct _stat64 stat_t;
static int stat_path(const char *p, stat_t *st) { return _stat64(p, st); }
static int access_path(const char *p, int mode) { return _access(p, mode); }
static int mkdir_path(const char *p, int mode_unused) { (void)mode_unused; return _mkdir(p); }

#define fseeko _fseeki64
#define ftello _ftelli64

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

#define PATH_SEP '\\'

#else

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef int sock_t;
#define SOCK_INVALID (-1)
#define close_sock(s) close(s)

static void sleep_us(unsigned int us) { usleep(us); }
static void sleep_ms(unsigned int ms) { usleep(ms * 1000U); }

#define perror_sock perror

typedef struct stat stat_t;
static int stat_path(const char *p, stat_t *st) { return stat(p, st); }
static int access_path(const char *p, int mode) { return access(p, mode); }
static int mkdir_path(const char *p, int mode) { return mkdir(p, (mode_t)mode); }

#define PATH_SEP '/'

#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CTRL_RX_BUFSZ              8192
#define PEER_RX_BUFSZ              8192
#define CTRL_TYPE_LEN              8
#define PEER_TYPE_LEN              12
#define KEEPALIVE_INTERVAL_MS      15000
#define INITIAL_PUNCH_COUNT        5
#define INITIAL_PUNCH_INTERVAL_MS  500
#define MAX_MESSAGE_STORE          4096
#define MAX_SHAREABLE_ENDPOINT     160
#define FILE_CHUNK_SIZE            1024
#define TRANSFER_ID_LEN            16
#define RECEIVED_DIR_NAME          "ReceivedFiles"

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

#define PEER_MSG      "TEXTMSG-----"
#define PEER_EXIT     "EXITPEER----"
#define PEER_PING     "PINGPEER----"
#define PEER_FILEMETA "FILEMETA----"
#define PEER_FILECHNK "FILECHNK----"

typedef enum {
    MODE_NONE = 0,
    MODE_LOCAL,
    MODE_PUBLIC
} peer_mode_t;

typedef struct {
    int active;
    uint8_t transfer_id[TRANSFER_ID_LEN];
    uint64_t filesize;
    uint32_t total_chunks;
    uint32_t received_chunks;
    char filename[512];
    char output_path[PATH_MAX];
    FILE *fp;
    uint8_t *received_map;
} incoming_file_t;

typedef struct {
    sock_t control_sock;
    sock_t peer_sock;
    peer_mode_t mode;
    int peer_socket_ready;
    int remote_peer_ready;
    struct sockaddr_in6 python_addr;
    struct sockaddr_in6 remote_peer;
    long long last_keepalive_ms;
    long long last_punch_ms;
    int punches_left;
    char shareable_endpoint[MAX_SHAREABLE_ENDPOINT];
    char latest_message[MAX_MESSAGE_STORE];
    int latest_message_ready;
    incoming_file_t incoming_file;
} app_state_t;

static long long now_ms(void) {
#if defined(_WIN32)
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
#endif
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |
           (uint64_t)p[7];
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

static int random_bytes(uint8_t *dst, size_t n) {
#if defined(_WIN32)
    if (n == 0) return 0;
    if (BCryptGenRandom(NULL, dst, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        return 0;
    }
    // Fallback: rand_s (still crypto-backed on Windows).
    for (size_t i = 0; i < n; i++) {
        unsigned int r = 0;
        if (rand_s(&r) != 0) return -1;
        dst[i] = (uint8_t)(r & 0xff);
    }
    return 0;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        size_t got = 0;
        while (got < n) {
            ssize_t r = read(fd, dst + got, n - got);
            if (r <= 0) {
                close(fd);
                return -1;
            }
            got += (size_t)r;
        }
        close(fd);
        return 0;
    }

    srand((unsigned int)(time(NULL) ^ getpid()));
    for (size_t i = 0; i < n; i++) {
        dst[i] = (uint8_t)(rand() & 0xff);
    }
    return 0;
#endif
}

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

static int send_typed_packet_data(sock_t sock,
                                  const struct sockaddr_in6 *to,
                                  const char *type,
                                  size_t type_len,
                                  const uint8_t *payload,
                                  size_t payload_len,
                                  size_t max_buf) {
    uint8_t buf[PEER_RX_BUFSZ];
    if (type_len + payload_len > max_buf || type_len + payload_len > sizeof(buf)) {
        errno = EMSGSIZE;
        return -1;
    }

    memcpy(buf, type, type_len);
    if (payload_len > 0 && payload != NULL) {
        memcpy(buf + type_len, payload, payload_len);
    }

    int sent = (int)sendto(sock, buf, type_len + payload_len, 0,
                          (const struct sockaddr *)to, sizeof(*to));
    return (sent == (ssize_t)(type_len + payload_len)) ? 0 : -1;
}

static int send_typed_packet_text(sock_t sock,
                                  const struct sockaddr_in6 *to,
                                  const char *type,
                                  size_t type_len,
                                  const char *payload,
                                  size_t max_buf) {
    const uint8_t *data = (const uint8_t *)(payload ? payload : "");
    size_t payload_len = payload ? strlen(payload) : 0;
    return send_typed_packet_data(sock, to, type, type_len, data, payload_len, max_buf);
}

static int recv_typed_packet(sock_t sock,
                             size_t type_len,
                             char *out_type,
                             uint8_t *out_payload,
                             size_t out_payload_sz,
                             size_t *out_payload_len,
                             struct sockaddr_in6 *from,
                             socklen_t *fromlen) {
    uint8_t buf[PEER_RX_BUFSZ];
    int n = (int)recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)from, fromlen);
    if (n < 0) return -1;
    if ((size_t)n < type_len) {
        errno = EPROTO;
        return -1;
    }

    memcpy(out_type, buf, type_len);
    out_type[type_len] = '\0';

    size_t payload_len = (size_t)n - type_len;
    if (payload_len > out_payload_sz) payload_len = out_payload_sz;
    if (payload_len > 0) {
        memcpy(out_payload, buf + type_len, payload_len);
    }
    if (out_payload_len) *out_payload_len = payload_len;
    return 0;
}

static sock_t bind_udp_ipv6(const char *local_ip, uint16_t port) {
    sock_t s = socket(AF_INET6, SOCK_DGRAM, 0);
#if defined(_WIN32)
    if (s == SOCK_INVALID) {
        perror_sock("socket(AF_INET6,SOCK_DGRAM)");
        return SOCK_INVALID;
    }
#else
    if (s < 0) {
        perror_sock("socket(AF_INET6,SOCK_DGRAM)");
        return SOCK_INVALID;
    }
#endif

    int one = 1;
#if defined(_WIN32)
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    (void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&one, sizeof(one));
#else
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    (void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
#endif

    struct sockaddr_in6 local;
    memset(&local, 0, sizeof(local));
    local.sin6_family = AF_INET6;
    local.sin6_port = htons(port);
    if (inet_pton(AF_INET6, local_ip, &local.sin6_addr) != 1) {
        close_sock(s);
        return SOCK_INVALID;
    }

    if (bind(s, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror_sock("bind(UDP IPv6)");
        close_sock(s);
        return SOCK_INVALID;
    }

    return s;
}

static int get_local_socket_endpoint(sock_t s, struct sockaddr_in6 *out) {
    socklen_t len = sizeof(*out);
    if (getsockname(s, (struct sockaddr *)out, &len) < 0) {
        perror_sock("getsockname");
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
    if (!end || *end != '\0' || port_ul > 65535UL || port_ul == 0) return -1;

    memset(out, 0, sizeof(*out));
    out->sin6_family = AF_INET6;
    out->sin6_port = htons((uint16_t)port_ul);
    if (inet_pton(AF_INET6, ip, &out->sin6_addr) != 1) return -1;
    return 0;
}

static int chosen_source_ipv6(struct in6_addr *out_addr) {
    const char *dst_ip = "2606:4700:4700::1111";
    const uint16_t dst_port = 53;

    sock_t s = socket(AF_INET6, SOCK_DGRAM, 0);
#if defined(_WIN32)
    if (s == SOCK_INVALID) return -1;
#else
    if (s < 0) return -1;
#endif

    struct sockaddr_in6 dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin6_family = AF_INET6;
    dst.sin6_port = htons(dst_port);
    if (inet_pton(AF_INET6, dst_ip, &dst.sin6_addr) != 1) {
        close_sock(s);
        return -2;
    }

    if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        close_sock(s);
        return -3;
    }

    struct sockaddr_in6 local;
    socklen_t len = sizeof(local);
    if (getsockname(s, (struct sockaddr *)&local, &len) < 0) {
        close_sock(s);
        return -4;
    }

    close_sock(s);
    if (out_addr) *out_addr = local.sin6_addr;
    return 0;
}

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
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_addrlen >= sizeof(struct sockaddr_in6)) {
            memcpy(out, rp->ai_addr, sizeof(struct sockaddr_in6));
            found = 1;
            break;
        }
    }

    freeaddrinfo(res);
    return found ? 0 : -2;
}

static int stun_ipv6_mapped_on_socket(sock_t s,
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
        if (sendto(s, req, sizeof(req), 0,
                   (const struct sockaddr *)stun_addr, sizeof(*stun_addr)) != (ssize_t)sizeof(req)) {
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int sel = select(s + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        uint8_t resp[1500];
        struct sockaddr_in6 from;
        socklen_t from_len = sizeof(from);
        int n = (int)recvfrom(s, resp, sizeof(resp), 0, (struct sockaddr *)&from, &from_len);
        if (n < 20) continue;

        uint16_t msg_type = read_be16(resp + 0);
        uint16_t msg_len = read_be16(resp + 2);
        uint32_t cookie = read_be32(resp + 4);

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

static void notify_python(app_state_t *st, const char *type, const char *text) {
    if (send_typed_packet_text(st->control_sock, &st->python_addr, type, CTRL_TYPE_LEN,
                               text ? text : "", CTRL_RX_BUFSZ) != 0) {
        perror_sock("sendto(Python control packet)");
    }
}

static void notify_info(app_state_t *st, const char *text) {
    notify_python(st, PKT_INFO, text);
}

static void clear_latest_message(app_state_t *st) {
    st->latest_message[0] = '\0';
    st->latest_message_ready = 0;
}

static void store_latest_message(app_state_t *st, const uint8_t *payload, size_t payload_len) {
    size_t copy_len = payload_len;
    if (copy_len >= sizeof(st->latest_message)) {
        copy_len = sizeof(st->latest_message) - 1;
    }
    memcpy(st->latest_message, payload, copy_len);
    st->latest_message[copy_len] = '\0';
    st->latest_message_ready = 1;
}

static void incoming_file_cleanup(incoming_file_t *inf) {
    if (inf->fp != NULL) {
        fclose(inf->fp);
        inf->fp = NULL;
    }
    free(inf->received_map);
    inf->received_map = NULL;
    memset(inf->transfer_id, 0, sizeof(inf->transfer_id));
    inf->active = 0;
    inf->filesize = 0;
    inf->total_chunks = 0;
    inf->received_chunks = 0;
    inf->filename[0] = '\0';
    inf->output_path[0] = '\0';
}

static int ensure_received_dir(char *out_dir, size_t out_dir_sz) {
    if (snprintf(out_dir, out_dir_sz, "%s", RECEIVED_DIR_NAME) >= (int)out_dir_sz) {
        return -1;
    }
    stat_t st;
    if (stat_path(out_dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) return -2;
        return 0;
    }
    if (mkdir_path(out_dir, 0777) != 0 && errno != EEXIST) {
        return -3;
    }
    return 0;
}

static int make_unique_output_path(const char *dir, const char *filename, char *out, size_t out_sz) {
    if (snprintf(out, out_sz, "%s%c%s", dir, PATH_SEP, filename) >= (int)out_sz) return -1;
    if (access_path(out, F_OK) != 0) return 0;

    const char *dot = strrchr(filename, '.');
    char base[512];
    char ext[128];
    if (dot != NULL && dot != filename) {
        size_t base_len = (size_t)(dot - filename);
        if (base_len >= sizeof(base)) base_len = sizeof(base) - 1;
        memcpy(base, filename, base_len);
        base[base_len] = '\0';
        strncpy(ext, dot, sizeof(ext) - 1);
        ext[sizeof(ext) - 1] = '\0';
    } else {
        strncpy(base, filename, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        ext[0] = '\0';
    }

    for (int i = 1; i < 100000; i++) {
        if (snprintf(out, out_sz, "%s%c%s (%d)%s", dir, PATH_SEP, base, i, ext) >= (int)out_sz) {
            return -2;
        }
        if (access_path(out, F_OK) != 0) return 0;
    }
    return -3;
}

static void finalize_incoming_file(app_state_t *st) {
    incoming_file_t *inf = &st->incoming_file;
    if (!inf->active) return;

    if (inf->fp != NULL) {
        fflush(inf->fp);
        fclose(inf->fp);
        inf->fp = NULL;
    }

    stat_t stbuf;
    char info[PATH_MAX + 96];
    if (stat_path(inf->output_path, &stbuf) == 0) {
        if ((uint64_t)stbuf.st_size == inf->filesize) {
            snprintf(info, sizeof(info), "File received: %s", inf->output_path);
        } else {
            snprintf(info, sizeof(info), "File received with size mismatch: %s", inf->output_path);
        }
    } else {
        snprintf(info, sizeof(info), "File receive completed: %s", inf->output_path);
    }
    notify_info(st, info);
    incoming_file_cleanup(inf);
}

static void abort_incoming_file_with_notice(app_state_t *st, const char *message) {
    incoming_file_cleanup(&st->incoming_file);
    notify_info(st, message);
}

static int handle_incoming_file_meta(app_state_t *st, const uint8_t *payload, size_t payload_len) {
    if (payload_len < TRANSFER_ID_LEN + 8 + 4 + 2) return -1;

    const uint8_t *p = payload;
    uint8_t transfer_id[TRANSFER_ID_LEN];
    memcpy(transfer_id, p, TRANSFER_ID_LEN);
    p += TRANSFER_ID_LEN;

    uint64_t filesize = read_be64(p);
    p += 8;
    uint32_t total_chunks = read_be32(p);
    p += 4;
    uint16_t name_len = read_be16(p);
    p += 2;

    if ((size_t)(p - payload) + name_len > payload_len) return -1;
    if (name_len == 0 || name_len >= sizeof(st->incoming_file.filename)) return -1;

    incoming_file_cleanup(&st->incoming_file);

    char dir[PATH_MAX];
    if (ensure_received_dir(dir, sizeof(dir)) != 0) {
        notify_info(st, "Failed to create ReceivedFiles directory.");
        return -1;
    }

    incoming_file_t *inf = &st->incoming_file;
    memcpy(inf->transfer_id, transfer_id, TRANSFER_ID_LEN);
    inf->filesize = filesize;
    inf->total_chunks = total_chunks;
    inf->received_chunks = 0;
    memcpy(inf->filename, p, name_len);
    inf->filename[name_len] = '\0';

    if (make_unique_output_path(dir, inf->filename, inf->output_path, sizeof(inf->output_path)) != 0) {
        abort_incoming_file_with_notice(st, "Failed to allocate output path in ReceivedFiles.");
        return -1;
    }

    inf->fp = fopen(inf->output_path, "wb+");
    if (inf->fp == NULL) {
        abort_incoming_file_with_notice(st, "Failed to open output file in ReceivedFiles.");
        return -1;
    }

    if (total_chunks > 0) {
        inf->received_map = (uint8_t *)calloc(total_chunks, 1);
        if (inf->received_map == NULL) {
            abort_incoming_file_with_notice(st, "Failed to allocate file receive buffer.");
            return -1;
        }
    }

    inf->active = 1;

    if (total_chunks == 0) {
        finalize_incoming_file(st);
    }
    return 0;
}

static int handle_incoming_file_chunk(app_state_t *st, const uint8_t *payload, size_t payload_len) {
    incoming_file_t *inf = &st->incoming_file;
    if (!inf->active) return -1;
    if (payload_len < TRANSFER_ID_LEN + 4 + 2) return -1;

    const uint8_t *p = payload;
    if (memcmp(p, inf->transfer_id, TRANSFER_ID_LEN) != 0) return -1;
    p += TRANSFER_ID_LEN;

    uint32_t chunk_index = read_be32(p);
    p += 4;
    uint16_t data_len = read_be16(p);
    p += 2;

    if ((size_t)(p - payload) + data_len != payload_len) return -1;
    if (chunk_index >= inf->total_chunks) return -1;
    if (inf->received_map != NULL && inf->received_map[chunk_index]) return 0;

    if (inf->fp == NULL) return -1;

    long long offset = (long long)chunk_index * (long long)FILE_CHUNK_SIZE;
    if (fseeko(inf->fp, offset, SEEK_SET) != 0) return -1;
    if (data_len > 0 && fwrite(p, 1, data_len, inf->fp) != data_len) return -1;
    fflush(inf->fp);

    if (inf->received_map != NULL) {
        inf->received_map[chunk_index] = 1;
    }
    inf->received_chunks++;

    if (inf->received_chunks >= inf->total_chunks) {
        finalize_incoming_file(st);
    }
    return 0;
}

static const char *path_basename_const(const char *path) {
    const char *slash1 = strrchr(path, '/');
    const char *slash2 = strrchr(path, '\\');
    const char *slash = slash1;
    if (slash2 != NULL && (slash1 == NULL || slash2 > slash1)) slash = slash2;
    return slash ? slash + 1 : path;
}

static int send_file_to_peer(app_state_t *st, const char *path) {
    if (!st->peer_socket_ready || !st->remote_peer_ready) {
        notify_info(st, "Peer session is not configured yet.");
        return -1;
    }
    if (path == NULL || path[0] == '\0') {
        notify_info(st, "No file path was provided.");
        return -1;
    }

    stat_t stbuf;
    if (stat_path(path, &stbuf) != 0 || !S_ISREG(stbuf.st_mode)) {
        notify_info(st, "File path is not a regular file.");
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        notify_info(st, "Failed to open the requested file.");
        return -1;
    }

    uint64_t filesize = (uint64_t)stbuf.st_size;
    uint32_t total_chunks = (uint32_t)((filesize + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
    const char *filename = path_basename_const(path);
    size_t filename_len = strlen(filename);
    if (filename_len == 0 || filename_len > 500) {
        fclose(fp);
        notify_info(st, "Filename is invalid or too long.");
        return -1;
    }

    uint8_t transfer_id[TRANSFER_ID_LEN];
    if (random_bytes(transfer_id, sizeof(transfer_id)) != 0) {
        fclose(fp);
        notify_info(st, "Failed to generate a transfer id.");
        return -1;
    }

    uint8_t meta[PEER_RX_BUFSZ];
    size_t meta_len = 0;
    memcpy(meta + meta_len, transfer_id, TRANSFER_ID_LEN);
    meta_len += TRANSFER_ID_LEN;
    write_be64(meta + meta_len, filesize);
    meta_len += 8;
    write_be32(meta + meta_len, total_chunks);
    meta_len += 4;
    write_be16(meta + meta_len, (uint16_t)filename_len);
    meta_len += 2;
    memcpy(meta + meta_len, filename, filename_len);
    meta_len += filename_len;

    if (send_typed_packet_data(st->peer_sock, &st->remote_peer, PEER_FILEMETA, PEER_TYPE_LEN,
                               meta, meta_len, PEER_RX_BUFSZ) != 0) {
        fclose(fp);
        notify_info(st, "Failed to send file metadata to the remote peer.");
        return -1;
    }

    uint8_t chunk_buf[TRANSFER_ID_LEN + 4 + 2 + FILE_CHUNK_SIZE];
    uint32_t chunk_index = 0;
    while (!feof(fp)) {
        size_t nread = fread(chunk_buf + TRANSFER_ID_LEN + 4 + 2, 1, FILE_CHUNK_SIZE, fp);
        if (nread == 0) {
            if (ferror(fp)) {
                fclose(fp);
                notify_info(st, "Failed while reading file data.");
                return -1;
            }
            break;
        }

        memcpy(chunk_buf, transfer_id, TRANSFER_ID_LEN);
        write_be32(chunk_buf + TRANSFER_ID_LEN, chunk_index);
        write_be16(chunk_buf + TRANSFER_ID_LEN + 4, (uint16_t)nread);

        if (send_typed_packet_data(st->peer_sock, &st->remote_peer, PEER_FILECHNK, PEER_TYPE_LEN,
                                   chunk_buf, TRANSFER_ID_LEN + 4 + 2 + nread, PEER_RX_BUFSZ) != 0) {
            fclose(fp);
            notify_info(st, "Failed to send a file chunk to the remote peer.");
            return -1;
        }

        chunk_index++;
        sleep_us(3000);
    }

    fclose(fp);
    notify_info(st, "File transfer packets sent.");
    return 0;
}

static void close_peer_socket_if_open(app_state_t *st) {
    if (st->peer_sock != SOCK_INVALID) {
        close_sock(st->peer_sock);
        st->peer_sock = SOCK_INVALID;
    }
    st->peer_socket_ready = 0;
    st->remote_peer_ready = 0;
    st->mode = MODE_NONE;
    st->punches_left = 0;
    st->shareable_endpoint[0] = '\0';
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
    if (st->peer_sock == SOCK_INVALID) return -1;

    st->mode = MODE_LOCAL;
    st->peer_socket_ready = 1;

    if (build_shareable_endpoint_text(st, st->shareable_endpoint, sizeof(st->shareable_endpoint)) != 0) {
        return -2;
    }
    notify_python(st, PKT_MYENDP, st->shareable_endpoint);
    notify_info(st, "Local loopback peer socket created.");
    return 0;
}

static int create_peer_socket_public(app_state_t *st) {
    close_peer_socket_if_open(st);

    st->peer_sock = bind_udp_ipv6("::", 0);
    if (st->peer_sock == SOCK_INVALID) return -1;

    st->mode = MODE_PUBLIC;
    st->peer_socket_ready = 1;

    if (build_shareable_endpoint_text(st, st->shareable_endpoint, sizeof(st->shareable_endpoint)) != 0) {
        return -2;
    }
    notify_python(st, PKT_MYENDP, st->shareable_endpoint);
    notify_info(st, "Public peer socket created.");
    return 0;
}

static void schedule_initial_punches(app_state_t *st) {
    st->punches_left = INITIAL_PUNCH_COUNT;
    st->last_punch_ms = 0;
    st->last_keepalive_ms = now_ms();
}

static void handle_python_control_packet(app_state_t *st,
                                         const char type[CTRL_TYPE_LEN + 1],
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
            notify_info(st, "Create the peer socket first.");
            return;
        }
        if (parse_endpoint_text(payload, &st->remote_peer) != 0) {
            notify_info(st, "Remote peer endpoint was invalid. Expected [ipv6]:port.");
            return;
        }
        st->remote_peer_ready = 1;
        schedule_initial_punches(st);
        notify_info(st, "Remote peer endpoint saved.");
        return;
    }

    if (strcmp(type, PKT_MSG) == 0) {
        if (!st->peer_socket_ready || !st->remote_peer_ready) {
            notify_info(st, "Peer session is not configured yet.");
            return;
        }
        if (send_typed_packet_text(st->peer_sock, &st->remote_peer, PEER_MSG, PEER_TYPE_LEN,
                                   payload, PEER_RX_BUFSZ) != 0) {
            notify_info(st, "Failed to send chat packet to the remote peer.");
        }
        return;
    }

    if (strcmp(type, PKT_SNDFILE) == 0) {
        (void)send_file_to_peer(st, payload);
        return;
    }

    if (strcmp(type, PKT_GETMSG) == 0) {
        if (st->latest_message_ready) {
            notify_python(st, PKT_MSG, st->latest_message);
            clear_latest_message(st);
        } else {
            notify_info(st, "GETMSG:EMPTY");
        }
        return;
    }

    if (strcmp(type, PKT_GETENDP) == 0) {
        if (st->shareable_endpoint[0] == '\0') {
            notify_info(st, "No shareable endpoint is available yet.");
        } else {
            notify_python(st, PKT_MYENDP, st->shareable_endpoint);
        }
        return;
    }

    if (strcmp(type, PKT_EXIT) == 0) {
        if (st->peer_socket_ready && st->remote_peer_ready) {
            (void)send_typed_packet_text(st->peer_sock, &st->remote_peer, PEER_EXIT, PEER_TYPE_LEN,
                                         "", PEER_RX_BUFSZ);
        }
        notify_info(st, "Local user requested exit. Closing this bridge.");
        *should_exit = 1;
        return;
    }

    notify_info(st, "Unknown Python control packet type received.");
}

static void handle_peer_packet(app_state_t *st,
                               const char type[PEER_TYPE_LEN + 1],
                               const uint8_t *payload,
                               size_t payload_len,
                               int *should_exit) {
    if (strcmp(type, PEER_MSG) == 0) {
        store_latest_message(st, payload, payload_len);
        return;
    }

    if (strcmp(type, PEER_EXIT) == 0) {
        notify_info(st, "The remote peer ended the session. Closing this bridge.");
        *should_exit = 1;
        return;
    }

    if (strcmp(type, PEER_PING) == 0) {
        return;
    }

    if (strcmp(type, PEER_FILEMETA) == 0) {
        if (handle_incoming_file_meta(st, payload, payload_len) != 0) {
            notify_info(st, "Ignored malformed incoming file metadata.");
        }
        return;
    }

    if (strcmp(type, PEER_FILECHNK) == 0) {
        if (handle_incoming_file_chunk(st, payload, payload_len) != 0) {
            notify_info(st, "Ignored malformed incoming file chunk.");
        }
        return;
    }
}

static void maybe_send_periodic_peer_packets(app_state_t *st) {
    if (!st->peer_socket_ready || !st->remote_peer_ready) return;

    long long now = now_ms();

    if (st->punches_left > 0) {
        if (st->last_punch_ms == 0 || (now - st->last_punch_ms) >= INITIAL_PUNCH_INTERVAL_MS) {
            (void)send_typed_packet_text(st->peer_sock, &st->remote_peer, PEER_PING, PEER_TYPE_LEN,
                                         "hello", PEER_RX_BUFSZ);
            st->last_punch_ms = now;
            st->punches_left--;
        }
    }

    if ((now - st->last_keepalive_ms) >= KEEPALIVE_INTERVAL_MS) {
        (void)send_typed_packet_text(st->peer_sock, &st->remote_peer, PEER_PING, PEER_TYPE_LEN,
                                     "keepalive", PEER_RX_BUFSZ);
        st->last_keepalive_ms = now;
    }
}

static int run_bridge_loop(app_state_t *st) {
    int should_exit = 0;

    while (!should_exit) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(st->control_sock, &rfds);

#if !defined(_WIN32)
        int maxfd = (int)st->control_sock;
        if (st->peer_socket_ready) {
            FD_SET(st->peer_sock, &rfds);
            if ((int)st->peer_sock > maxfd) maxfd = (int)st->peer_sock;
        }
#else
        if (st->peer_socket_ready) {
            FD_SET(st->peer_sock, &rfds);
        }
#endif

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000;

#if defined(_WIN32)
        int sel = select(0, &rfds, NULL, NULL, &tv);
        if (sel == SOCKET_ERROR) {
            perror_sock("select");
            return 1;
        }
#else
        int sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return 1;
        }
#endif

        if (sel > 0) {
            if (FD_ISSET(st->control_sock, &rfds)) {
                char type[CTRL_TYPE_LEN + 1];
                uint8_t payload[CTRL_RX_BUFSZ];
                size_t payload_len = 0;
                struct sockaddr_in6 from;
                socklen_t fromlen = sizeof(from);

                if (recv_typed_packet(st->control_sock, CTRL_TYPE_LEN, type,
                                      payload, sizeof(payload) - 1, &payload_len,
                                      &from, &fromlen) == 0) {
                    payload[payload_len] = '\0';
                    handle_python_control_packet(st, type, (const char *)payload, &should_exit);
                }
            }

            if (st->peer_socket_ready && FD_ISSET(st->peer_sock, &rfds)) {
                char type[PEER_TYPE_LEN + 1];
                uint8_t payload[PEER_RX_BUFSZ];
                size_t payload_len = 0;
                struct sockaddr_in6 from;
                socklen_t fromlen = sizeof(from);

                if (recv_typed_packet(st->peer_sock, PEER_TYPE_LEN, type,
                                      payload, sizeof(payload), &payload_len,
                                      &from, &fromlen) == 0) {
                    handle_peer_packet(st, type, payload, payload_len, &should_exit);
                }
            }
        }

        maybe_send_periodic_peer_packets(st);
    }

    return 0;
}

int main(int argc, char **argv) {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\\n");
        return 1;
    }
#endif
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <python_recv_port>\n", argv[0]);
        return 1;
    }

    char *end = NULL;
    unsigned long py_port_ul = strtoul(argv[1], &end, 10);
    if (!end || *end != '\0' || py_port_ul == 0 || py_port_ul > 65535UL) {
        fprintf(stderr, "Invalid python_recv_port: %s\n", argv[1]);
        return 1;
    }

    app_state_t st;
    memset(&st, 0, sizeof(st));
    st.control_sock = SOCK_INVALID;
    st.peer_sock = SOCK_INVALID;
    st.mode = MODE_NONE;
    clear_latest_message(&st);

    memset(&st.python_addr, 0, sizeof(st.python_addr));
    st.python_addr.sin6_family = AF_INET6;
    st.python_addr.sin6_port = htons((uint16_t)py_port_ul);
    if (inet_pton(AF_INET6, "::1", &st.python_addr.sin6_addr) != 1) {
        fprintf(stderr, "Failed to build Python address ::1\n");
        return 1;
    }

    st.control_sock = bind_udp_ipv6("::1", 0);
    if (st.control_sock == SOCK_INVALID) return 1;

    struct sockaddr_in6 control_local;
    if (get_local_socket_endpoint(st.control_sock, &control_local) != 0) {
        close_sock(st.control_sock);
        return 1;
    }

    char ctl_port_text[32];
    snprintf(ctl_port_text, sizeof(ctl_port_text), "%u", ntohs(control_local.sin6_port));
    notify_python(&st, PKT_CTLPORT, ctl_port_text);
    notify_info(&st, "Bridge started. Waiting for Python commands.");

    int rc = run_bridge_loop(&st);

    incoming_file_cleanup(&st.incoming_file);
    close_peer_socket_if_open(&st);
    if (st.control_sock >= 0) close_sock(st.control_sock);
    return rc;
}