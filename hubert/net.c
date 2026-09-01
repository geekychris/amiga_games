// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/* net.c — Amiga bsdsocket implementation + host-side stub. */

#include "net.h"

#include <string.h>
#include <stdlib.h>

#ifdef HAVE_AMIGA_DOS

/* ─── Amiga implementation ────────────────────────────────────────────── */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

/* SocketBase is declared extern in <proto/socket.h> above — the linker
 * fills it in from libamiga (or wherever the bsdsocket glue lives). We
 * only need to Open the library and check the pointer. */
struct Library *SocketBase = 0;

struct NetConn {
    long sock;   /* LONG under bsdsocket */
};

int net_startup(void)
{
    if (SocketBase) return 0;
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    return SocketBase ? 0 : -1;
}

void net_shutdown(void)
{
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = 0; }
}

/* Wait up to timeout_ms on a socket for read/write readiness. Returns:
 *   >0  socket ready
 *    0  timeout
 *   -1  error */
static int wait_ready(long s, int for_write, int timeout_ms)
{
    fd_set fds;
    struct timeval tv;
    long r;
    if (timeout_ms < 0) timeout_ms = 0;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (for_write) r = WaitSelect(s + 1, 0, &fds, 0, &tv, 0);
    else            r = WaitSelect(s + 1, &fds, 0, 0, &tv, 0);
    return (int)r;
}

/* Wall-clock in ms via bsdsocket's gettimeofday (available under
 * bsdsocket.library ≥3 which we already require). */
static long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000L + (long)tv.tv_usec / 1000L;
}

/* Remaining ms from an absolute deadline. Clamped to >=0. */
static int remaining_ms(long deadline)
{
    long r = deadline - now_ms();
    if (r < 0) r = 0;
    return (int)r;
}

NetConn *net_connect(const char *host, int port, int timeout_ms, NetResult *err)
{
    struct hostent *he;
    struct sockaddr_in sa;
    long s;
    long one = 1;
    NetConn *c;
    int ready;
    long deadline;
    int rem;

    if (!SocketBase && net_startup() != 0) { if (err) *err = NET_ERR_NO_LIB; return 0; }

    /* Single deadline covers DNS + connect + subsequent writes so slow
     * DNS doesn't burn all our time and leave connect() with nothing. */
    deadline = now_ms() + (long)timeout_ms;

    he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        if (err) *err = NET_ERR_RESOLVE;
        return 0;
    }

    /* DNS could have exhausted the budget on its own. */
    rem = remaining_ms(deadline);
    if (rem == 0) {
        if (err) *err = NET_ERR_TIMEOUT;
        return 0;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        if (err) *err = NET_ERR_CONNECT;
        return 0;
    }

    /* Non-blocking so we can enforce our timeout. IoctlSocket returns
     * <0 on failure — treat as a hard error rather than continuing with
     * a blocking socket that would ignore our deadline. */
    if (IoctlSocket(s, FIONBIO, (char *)&one) < 0) {
        CloseSocket(s);
        if (err) *err = NET_ERR_CONNECT;
        return 0;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        int e = Errno();
        if (e != EINPROGRESS && e != EWOULDBLOCK) {
            CloseSocket(s);
            if (err) *err = NET_ERR_CONNECT;
            return 0;
        }
    }
    ready = wait_ready(s, 1, remaining_ms(deadline));
    if (ready <= 0) {
        CloseSocket(s);
        if (err) *err = (ready == 0) ? NET_ERR_TIMEOUT : NET_ERR_CONNECT;
        return 0;
    }

    /* Even though select said the socket is writable, connect may still
     * have failed asynchronously. Query SO_ERROR and abort if non-zero
     * — otherwise we'd hand back a broken NetConn that later fails on
     * every send/recv without a clear reason. */
    {
        int soerr = 0;
        int optlen = (int)sizeof(soerr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr,
                       (LONG *)&optlen) < 0 || soerr != 0) {
            CloseSocket(s);
            if (err) *err = NET_ERR_CONNECT;
            return 0;
        }
    }

    c = (NetConn *)AllocVec(sizeof(NetConn), MEMF_ANY);
    if (!c) { CloseSocket(s); if (err) *err = NET_ERR_CONNECT; return 0; }
    c->sock = s;
    if (err) *err = NET_OK;
    return c;
}

NetResult net_send_all(NetConn *c, const char *buf, int len, int timeout_ms)
{
    int sent = 0;
    long deadline;
    /* Zero-length sends are always success — bail before entering the
     * loop so we can't spin on send() returning 0 with EWOULDBLOCK. */
    if (len <= 0) return NET_OK;
    deadline = now_ms() + (long)timeout_ms;
    while (sent < len) {
        long n;
        int rem = remaining_ms(deadline);
        int ready;
        if (rem == 0) return NET_ERR_TIMEOUT;
        ready = wait_ready(c->sock, 1, rem);
        if (ready == 0) return NET_ERR_TIMEOUT;
        if (ready < 0)  return NET_ERR_SEND;
        n = send(c->sock, (APTR)(buf + sent), len - sent, 0);
        if (n > 0) { sent += (int)n; continue; }
        if (n < 0) {
            int e = Errno();
            if (e == EINTR || e == EWOULDBLOCK) continue;
            return NET_ERR_SEND;
        }
        /* n == 0: peer effectively closed the write side — surface it. */
        return NET_ERR_SEND;
    }
    return NET_OK;
}

NetResult net_recv(NetConn *c, char *buf, int bufSize, int *out_len, int timeout_ms)
{
    long n;
    /* Loop while wait_ready keeps saying "ready" but recv keeps saying
     * "actually not ready" (EWOULDBLOCK) or "try again" (EINTR). Returning
     * NET_OK with *out_len==0 would let callers busy-spin on EOF-looking
     * data — surface a real timeout instead when the deadline expires. */
    for (;;) {
        int ready = wait_ready(c->sock, 0, timeout_ms);
        if (ready == 0) return NET_ERR_TIMEOUT;
        if (ready < 0)  return NET_ERR_RECV;
        n = recv(c->sock, buf, bufSize, 0);
        if (n == 0)  return NET_ERR_CLOSED;
        if (n > 0)   { *out_len = (int)n; return NET_OK; }
        {
            int e = Errno();
            if (e == EINTR || e == EWOULDBLOCK) continue;
            return NET_ERR_RECV;
        }
    }
}

void net_close(NetConn *c)
{
    if (!c) return;
    if (c->sock >= 0) CloseSocket(c->sock);
    FreeVec(c);
}

/* Stub interface is a no-op on Amiga side — never called there. */
void        net_stub_reset(void) {}
void        net_stub_queue_recv(const char *b)     { (void)b; }
void        net_stub_queue_recv_close(void)         {}
const char *net_stub_send_captured(int *n)          { if (n) *n = 0; return ""; }
const char *net_stub_last_host(void)                { return ""; }
int         net_stub_last_port(void)                { return 0; }

#else

/* ─── Host stub — scripted send/recv for unit tests ───────────────────── */

#define STUB_BUF 65536
static char g_stub_send[STUB_BUF];
static int  g_stub_send_len;
static char g_stub_recv[STUB_BUF];
static int  g_stub_recv_len;
static int  g_stub_recv_pos;
static int  g_stub_recv_closed;
static char g_stub_host[128];
static int  g_stub_port;

struct NetConn { int fake; };
static NetConn g_stub_conn;

int  net_startup(void)  { return 0; }
void net_shutdown(void) {}

void net_stub_reset(void)
{
    g_stub_send_len = 0; g_stub_send[0] = '\0';
    g_stub_recv_len = 0; g_stub_recv_pos = 0; g_stub_recv_closed = 0;
    g_stub_host[0] = '\0'; g_stub_port = 0;
}

void net_stub_queue_recv(const char *bytes)
{
    int n = 0;
    while (bytes[n] && g_stub_recv_len < STUB_BUF - 1) {
        g_stub_recv[g_stub_recv_len++] = bytes[n++];
    }
}
void net_stub_queue_recv_close(void) { g_stub_recv_closed = 1; }
const char *net_stub_send_captured(int *out) { if (out) *out = g_stub_send_len; return g_stub_send; }
const char *net_stub_last_host(void) { return g_stub_host; }
int         net_stub_last_port(void) { return g_stub_port; }

NetConn *net_connect(const char *host, int port, int timeout_ms, NetResult *err)
{
    (void)timeout_ms;
    strncpy(g_stub_host, host, sizeof(g_stub_host) - 1);
    g_stub_host[sizeof(g_stub_host) - 1] = '\0';
    g_stub_port = port;
    if (err) *err = NET_OK;
    return &g_stub_conn;
}

NetResult net_send_all(NetConn *c, const char *buf, int len, int timeout_ms)
{
    (void)c; (void)timeout_ms;
    if (g_stub_send_len + len >= STUB_BUF) return NET_ERR_SEND;
    memcpy(g_stub_send + g_stub_send_len, buf, len);
    g_stub_send_len += len;
    g_stub_send[g_stub_send_len] = '\0';
    return NET_OK;
}

NetResult net_recv(NetConn *c, char *buf, int bufSize, int *out_len, int timeout_ms)
{
    int n;
    (void)c; (void)timeout_ms;
    if (g_stub_recv_pos >= g_stub_recv_len) {
        if (g_stub_recv_closed) return NET_ERR_CLOSED;
        return NET_ERR_STUB_EOS;
    }
    n = g_stub_recv_len - g_stub_recv_pos;
    if (n > bufSize) n = bufSize;
    memcpy(buf, g_stub_recv + g_stub_recv_pos, n);
    g_stub_recv_pos += n;
    *out_len = n;
    return NET_OK;
}

void net_close(NetConn *c) { (void)c; }

#endif
