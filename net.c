/* SPDX-License-Identifier: MIT */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L /* getaddrinfo, strdup */
#include "net.h"

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "json.h"

#define TRIVIA_HOST "opentdb.com"
#define TRIVIA_PORT "443"

static const char *ca_bundle_paths[] = {
    "/etc/ssl/certs/ca-certificates.crt", /* Debian/Arch */
    "/etc/pki/tls/certs/ca-bundle.crt",   /* Fedora/RHEL */
    "/etc/ssl/cert.pem",                  /* Alpine/macOS-ish */
    "/usr/local/share/certs/ca-root-nss.crt",
};

/* ---------- minimal HTTP/1.1 over TLS ---------- */

typedef struct {
    mbedtls_net_context fd;
    mbedtls_ssl_context ssl;
    bool use_tls;
} conn;

static int conn_read(conn *c, unsigned char *buf, size_t len) {
    for (;;) {
        int n;
        if (c->use_tls)
            n = mbedtls_ssl_read(&c->ssl, buf, len);
        else
            n = (int)read(c->fd.fd, buf, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        return n;
    }
}

static int conn_write_all(conn *c, const unsigned char *buf, size_t len) {
    while (len > 0) {
        int n;
        if (c->use_tls)
            n = mbedtls_ssl_write(&c->ssl, buf, len);
        else
            n = (int)write(c->fd.fd, buf, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (n <= 0)
            return -1;
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

/* Protocol-level DNS resolution (res_query): honors /etc/resolv.conf without
 * ever dlopening NSS modules. Fully static binaries crash when getaddrinfo
 * pulls in external NSS modules (e.g. systemd-resolved), so the static build
 * (TRIVIA_STATIC) never touches getaddrinfo. The answer packet is parsed by
 * hand (ns_parserr is unavailable in static glibc archives). */
/* Skips one (possibly compressed) domain name; returns bytes consumed or -1. */
static int dns_skip_name(const unsigned char *p, const unsigned char *end) {
    const unsigned char *start = p;
    while (p < end) {
        const unsigned char len = *p;
        if (len == 0)
            return (int)(p - start) + 1;
        if ((len & 0xC0) == 0xC0)
            return (int)(p - start) + 2;
        p += 1 + len;
    }
    return -1;
}
static int resolve_host(const char *host, const char *port, struct sockaddr_storage *ss,
                        socklen_t *sslen) {
    static const struct {
        int type;
        int family;
        size_t length;
    } records[] = {{1, AF_INET, 4}, {28, AF_INET6, 16}}; /* ns_t_a, ns_t_aaaa */
    unsigned char answer[2048];
    for (size_t i = 0; i < sizeof records / sizeof records[0]; i++) {
        const int n = res_query(host, ns_c_in, records[i].type, answer, sizeof answer);
        if (n < 12)
            continue;
        const unsigned char *p = answer + 12; /* skip header */
        const unsigned char *end = answer + n;
        const unsigned short answers =
            (unsigned short)((answer[6] << 8) | answer[7]); /* ANCOUNT */
        /* skip the question section */
        for (int q = 0; q < ((answer[4] << 8) | answer[5]); q++) { /* QDCOUNT */
            const int skip = dns_skip_name(p, end);
            if (skip < 0 || end - p < skip + 4)
                goto next_type;
            p += skip + 4;
        }
        for (int r = 0; r < answers; r++) {
            int skip = dns_skip_name(p, end);
            if (skip < 0 || end - p < skip + 10)
                break;
            p += skip;
            const int type = (p[0] << 8) | p[1];
            const int rdlength = (p[8] << 8) | p[9];
            p += 10;
            if (end - p < rdlength)
                break;
            if (type == records[i].type && rdlength == (int)records[i].length) {
                memset(ss, 0, sizeof *ss);
                if (records[i].family == AF_INET) {
                    struct sockaddr_in *sin = (struct sockaddr_in *)ss;
                    sin->sin_family = AF_INET;
                    sin->sin_port = htons((uint16_t)atoi(port));
                    memcpy(&sin->sin_addr, p, 4);
                } else {
                    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
                    sin6->sin6_family = AF_INET6;
                    sin6->sin6_port = htons((uint16_t)atoi(port));
                    memcpy(&sin6->sin6_addr, p, 16);
                }
                *sslen = (socklen_t)(records[i].family == AF_INET
                                         ? sizeof(struct sockaddr_in)
                                         : sizeof(struct sockaddr_in6));
                return 0;
            }
            p += rdlength;
        }
    next_type:;
    }
#ifndef TRIVIA_STATIC
    /* Fallback for dynamic builds: full getaddrinfo semantics. */
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return -1;
    memcpy(ss, res->ai_addr, res->ai_addrlen);
    *sslen = res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
#else
    return -1;
#endif
}

/* Connects TCP (and TLS when use_tls) to host:port. */
static int conn_connect(conn *c, const char *host, const char *port, bool use_tls,
                        mbedtls_ssl_config *conf, char *err, size_t errcap) {
    memset(c, 0, sizeof *c);
    c->use_tls = use_tls;
    mbedtls_net_init(&c->fd);
    if (use_tls)
        mbedtls_ssl_init(&c->ssl);

    struct sockaddr_storage ss;
    socklen_t sslen = 0;
    if (resolve_host(host, port, &ss, &sslen) != 0) {
        snprintf(err, errcap, "cannot resolve %s", host);
        return -1;
    }
    int sfd = socket(ss.ss_family, SOCK_STREAM, 0);
    if (sfd < 0 || connect(sfd, (struct sockaddr *)&ss, sslen) != 0) {
        if (sfd >= 0)
            close(sfd);
        snprintf(err, errcap, "cannot connect to %s:%s", host, port);
        return -1;
    }
    c->fd.fd = sfd;
    if (!use_tls)
        return 0;

    if (mbedtls_ssl_setup(&c->ssl, conf) != 0) {
        snprintf(err, errcap, "ssl setup failed");
        return -1;
    }
    if (mbedtls_ssl_set_hostname(&c->ssl, host) != 0) {
        snprintf(err, errcap, "ssl hostname failed");
        return -1;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    for (;;) {
        const int r = mbedtls_ssl_handshake(&c->ssl);
        if (r == 0)
            break;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        snprintf(err, errcap, "tls handshake failed (-0x%04x)", (unsigned)-r);
        return -1;
    }
    if (mbedtls_ssl_get_verify_result(&c->ssl) != 0) {
        snprintf(err, errcap, "certificate verification failed");
        return -1;
    }
    return 0;
}

static void conn_close(conn *c) {
    if (c->use_tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
    }
    mbedtls_net_free(&c->fd);
}

static void append_bytes(char **buf, size_t *len, size_t *cap, const unsigned char *data,
                         size_t n) {
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap * 2 : 8192;
        while (ncap < *len + n + 1)
            ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb)
            return;
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
}

/* Performs one GET and returns the body in a malloc'd buffer. NULL on failure. */
static char *http_get(const char *host, const char *port, const char *path, bool use_tls,
                      mbedtls_ssl_config *conf, char *err, size_t errcap) {
    conn c;
    if (conn_connect(&c, host, port, use_tls, conf, err, errcap) != 0)
        return NULL;

    char request[512];
    snprintf(request, sizeof request,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: sprinkle-trivia-native/1.0\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);
    if (conn_write_all(&c, (const unsigned char *)request, strlen(request)) != 0) {
        snprintf(err, errcap, "request write failed");
        conn_close(&c);
        return NULL;
    }

    char *raw = NULL;
    size_t raw_len = 0, raw_cap = 0;
    for (;;) {
        unsigned char chunk[4096];
        const int n = conn_read(&c, chunk, sizeof chunk);
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0)
            break;
        if (n < 0)
            break;
        append_bytes(&raw, &raw_len, &raw_cap, chunk, (size_t)n);
        if (raw_len > 4u * 1024 * 1024)
            break; /* sanity bound */
    }
    conn_close(&c);
    if (!raw || raw_len == 0) {
        snprintf(err, errcap, "empty response");
        free(raw);
        return NULL;
    }

    /* split headers/body */
    char *body = NULL;
    char *header_end = strstr(raw, "\r\n\r\n");
    if (!header_end) {
        snprintf(err, errcap, "malformed response");
        free(raw);
        return NULL;
    }
    *header_end = '\0';
    body = header_end + 4;

    /* status */
    if (strncmp(raw, "HTTP/", 5) != 0) {
        snprintf(err, errcap, "malformed status line");
        free(raw);
        return NULL;
    }
    int status = atoi(raw + 9);
    if (status != 200) {
        snprintf(err, errcap, "HTTP status %d", status);
        free(raw);
        return NULL;
    }

    /* chunked transfer decoding */
    char lower_headers[4096];
    snprintf(lower_headers, sizeof lower_headers, "%s", raw);
    for (char *p = lower_headers; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    char *result = NULL;
    if (strstr(lower_headers, "transfer-encoding:") &&
        strstr(lower_headers, "chunked")) {
        char *out = NULL;
        size_t out_len = 0, out_cap = 0;
        char *p = body;
        for (;;) {
            /* chunk size line */
            char *eol = strstr(p, "\r\n");
            if (!eol)
                break;
            const size_t size = (size_t)strtoul(p, NULL, 16);
            p = eol + 2;
            if (size == 0)
                break;
            if (strlen(p) < size)
                break; /* truncated */
            append_bytes(&out, &out_len, &out_cap, (const unsigned char *)p, size);
            p += size;
            if (strncmp(p, "\r\n", 2) == 0)
                p += 2;
        }
        result = out;
        if (out)
            out[out_len] = '\0';
    } else {
        result = strdup(body);
    }
    free(raw);
    return result;
}

/* ---------- worker thread ---------- */

static void set_error(trivia_fetch *job, const char *msg) {
    snprintf(job->error, sizeof job->error, "%.*s", (int)sizeof job->error - 1, msg);
    job->ok = false;
    job->done = true;
}

static void *fetch_thread(void *arg) {
    trivia_fetch *job = arg;

    char request_path[64];
    snprintf(request_path, sizeof request_path, "/api.php?amount=%d", job->count);

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    char err[256] = {0};
    char *payload = NULL;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);

    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)"sprinkle-trivia", 15) != 0) {
        set_error(job, "rng seed failed");
        goto out;
    }
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        set_error(job, "ssl defaults failed");
        goto out;
    }
    bool have_ca = false;
    const char *ca_env = getenv("TRIVIA_CA_BUNDLE");
    if (ca_env && mbedtls_x509_crt_parse_file(&cacert, ca_env) == 0)
        have_ca = true;
    for (size_t i = 0; !have_ca && i < sizeof ca_bundle_paths / sizeof ca_bundle_paths[0]; i++)
        if (mbedtls_x509_crt_parse_file(&cacert, ca_bundle_paths[i]) == 0)
            have_ca = true;
    if (!have_ca) {
        set_error(job, "no CA bundle found (set TRIVIA_CA_BUNDLE)");
        goto out;
    }
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    payload = http_get(TRIVIA_HOST, TRIVIA_PORT, request_path, true, &conf, err, sizeof err);
    if (!payload) {
        char msg[300];
        snprintf(msg, sizeof msg, "fetch failed: %s", err);
        set_error(job, msg);
        goto out;
    }

    {
        trivia_question parsed[TRIVIA_MAX_QUESTIONS];
        const int n = json_parse_questions(payload, strlen(payload), parsed,
                                           TRIVIA_MAX_QUESTIONS);
        if (n <= 0) {
            set_error(job, "could not parse questions");
        } else {
            unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)job;
            for (int i = 0; i < n; i++)
                trivia_shuffle_options(&parsed[i], &seed);
            memcpy(job->qq, parsed, sizeof parsed);
            job->count = n;
            job->ok = true;
            job->done = true;
        }
    }

out:
    free(payload);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return NULL;
}

trivia_fetch *trivia_fetch_start(int amount) {
    trivia_fetch *job = calloc(1, sizeof *job);
    if (!job)
        return NULL;
    job->count = amount;
    pthread_t thread;
    if (pthread_create(&thread, NULL, fetch_thread, job) != 0) {
        free(job);
        return NULL;
    }
    pthread_detach(thread);
    return job;
}

void trivia_fetch_free(trivia_fetch *job) { free(job); }
