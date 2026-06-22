#include <iostream>
#include "unpthread.h"
#include "unp.h"
#include "msg.h"
using namespace std;

Thread * tptr;
socklen_t addrlen;
SOCKET listenfd;
int nthreads;

int main(int argc, char **argv)
{
    int i;
    void thread_make(int);

    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        err_quit("WSAStartup failed");
    }

    const char *host = "0.0.0.0";
    const char *port = "8888";
    nthreads = 4;

    listenfd = Tcp_listen(host, port, &addrlen);

    printf("server listening on %s:%s, acceptor threads: %d\n", host, port, nthreads);

    tptr = (Thread *)Calloc(nthreads, sizeof(Thread));

    for(i = 0; i < nthreads; i++)
    {
        thread_make(i);
    }

    for(;;)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    WSACleanup();
    return 0;
}

void thread_make(int i)
{
    void * thread_main(void *);
    int *arg = (int *)Calloc(1, sizeof(int));
    *arg = i;
    tptr[i].thread = std::thread(thread_main, arg);
}
