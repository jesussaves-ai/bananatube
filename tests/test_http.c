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
    test_done();
}
