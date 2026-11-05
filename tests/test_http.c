/* The HTTP deadline: a request that cannot finish in the thread's budget fails fast. */
#include "net/http.h"
#include "test.h"
#include "util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *slow_server(void *arg)
{
    int listener = *(int *)arg;
    int client = accept(listener, NULL, NULL);
    usleep(500 * 1000);
    if (client >= 0) close(client);
    return NULL;
}

/* A media URL as YouTube's CDN serves it: 206 to the open-ended range ffmpeg opens with, and a 403 to
 * anything else. The probe must send exactly that. */
static bool g_open_range;
static void *range_server(void *arg)
{
    int listener = *(int *)arg;
    int client = accept(listener, NULL, NULL);
    if (client < 0) return NULL;

    char req[2048];
    ssize_t n = recv(client, req, sizeof req - 1, 0);
    if (n > 0) {
        req[n] = '\0';
        g_open_range = str_contains(req, "Range: bytes=0-\r\n");
    }
    if (!g_open_range) {
        const char *no = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send(client, no, strlen(no), MSG_NOSIGNAL);
        close(client);
        return NULL;
    }
    const char *ok = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-1048575/1048576\r\n"
                     "Content-Length: 1048576\r\n\r\n";
    if (send(client, ok, strlen(ok), MSG_NOSIGNAL) < 0) { close(client); return NULL; }
    static char body[64 * 1024];
    for (int i = 0; i < 16; i++)
        if (send(client, body, sizeof body, MSG_NOSIGNAL) < 0) break;   /* the probe hangs up early */
    close(client);
    return NULL;
}

int main(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    check(listener >= 0);
    if (listener < 0) test_done();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    check(bind(listener, (struct sockaddr *)&addr, sizeof addr) == 0);
    check(listen(listener, 1) == 0);

    socklen_t len = sizeof addr;
    check(getsockname(listener, (struct sockaddr *)&addr, &len) == 0);

    pthread_t thread;
    check(pthread_create(&thread, NULL, slow_server, &listener) == 0);

    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%u/", (unsigned)ntohs(addr.sin_port));

    http_init();
    http_set_deadline_ms(now_ms() + 100);
    HttpResponse r;
    check(!http_get(url, 5000, &r));
    check(str_contains(http_error(&r), "timed out"));
    http_response_free(&r);
    http_set_deadline_ms(0);
    http_fini();

    pthread_join(thread, NULL);
    close(listener);

    /* The range probe: it must ask with ffmpeg's open-ended range, report the status, and not pull the body. */
    listener = socket(AF_INET, SOCK_STREAM, 0);
    check(listener >= 0);
    check(bind(listener, (struct sockaddr *)&addr, sizeof addr) == 0);
    check(listen(listener, 1) == 0);
    len = sizeof addr;
    check(getsockname(listener, (struct sockaddr *)&addr, &len) == 0);
    check(pthread_create(&thread, NULL, range_server, &listener) == 0);
    snprintf(url, sizeof url, "http://127.0.0.1:%u/", (unsigned)ntohs(addr.sin_port));

    http_init();
    check(http_get_range(url, 5000, &r));
    check(r.status == 206);
    check(r.len <= 4096);                 /* stopped at the probe's own limit */
    check(g_open_range);                  /* and it really sent "Range: bytes=0-" */
    http_response_free(&r);
    http_fini();
    pthread_join(thread, NULL);
    close(listener);

    test_done();
}
