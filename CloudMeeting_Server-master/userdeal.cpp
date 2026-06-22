#include "unpthread.h"
#include <stdlib.h>
#include "unp.h"
#include "netheader.h"
#include "msg.h"
#include <random>
#include <chrono>

std::mutex mlock;
extern socklen_t addrlen;
extern SOCKET    listenfd;
extern int       nthreads;
extern Thread   *tptr;

uint32_t generate_room_id()
{
    static std::mt19937 gen((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_int_distribution<uint32_t> dist(10000000, 99999999);
    uint32_t id;
    std::lock_guard<std::mutex> lk(g_roomsMutex);
    do {
        id = dist(gen);
    } while (g_rooms.count(id) > 0);
    return id;
}

void* thread_main(void *arg)
{
    void dowithuser(SOCKET connfd);
    int i = *(int *)arg;
    free(arg);

    SA *cliaddr;
    socklen_t clilen;
    cliaddr = (SA *)Calloc(1, addrlen);
    char buf[MAXSOCKADDR];
    for (;;)
    {
        clilen = addrlen;
        mlock.lock();
        SOCKET connfd = Accept(listenfd, cliaddr, &clilen);
        mlock.unlock();

        printf("connection from %s\n", Sock_ntop(buf, MAXSOCKADDR, cliaddr, clilen));
        dowithuser(connfd);
    }
    return NULL;
}

void dowithuser(SOCKET connfd)
{
    void writetofd(SOCKET fd, NetMsg msg);
    void room_thread_main(std::shared_ptr<Pool>);

    char head[15] = {0};
    while (1)
    {
        ssize_t ret = Readn(connfd, head, 11);
        if (ret <= 0)
        {
            Close(connfd);
            printf("%lld close\n", (long long)connfd);
            return;
        }
        else if (ret < 11)
        {
            printf("data len too short\n");
        }
        else if (head[0] != '$')
        {
            printf("data format error\n");
        }
        else
        {
            MSG_TYPE msgtype;
            memcpy(&msgtype, head + 1, 2);
            msgtype = (MSG_TYPE)ntohs(msgtype);

            uint32_t ip;
            memcpy(&ip, head + 3, 4);
            ip = ntohl(ip);

            uint32_t datasize;
            memcpy(&datasize, head + 7, 4);
            datasize = ntohl(datasize);

            if (msgtype == CREATE_MEETING)
            {
                char tail;
                Readn(connfd, &tail, 1);
                if (datasize == 0 && tail == '#')
                {
                    char *c = (char *)&ip;
                    printf("create meeting ip: %d.%d.%d.%d\n",
                           (unsigned char)c[3], (unsigned char)c[2],
                           (unsigned char)c[1], (unsigned char)c[0]);

                    uint32_t roomId = generate_room_id();
                    auto pool = std::make_shared<Pool>(roomId);

                    {
                        std::lock_guard<std::mutex> lk(g_roomsMutex);
                        g_rooms[roomId] = pool;
                    }

                    {
                        std::lock_guard<std::mutex> lk(pool->queueMutex);
                        ClientConn cc;
                        cc.cmd = 'C';
                        cc.sockfd = connfd;
                        pool->clientQueue.push(cc);
                    }
                    pool->queueCond.notify_one();

                    std::thread(room_thread_main, pool).detach();
                    printf("room %u created\n", roomId);
                    return;
                }
                else
                {
                    printf("1 data format error\n");
                }
            }
            else if (msgtype == JOIN_MEETING)
            {
                uint32_t msgsize, roomno;
                memcpy(&msgsize, head + 7, 4);
                msgsize = ntohl(msgsize);
                int r = Readn(connfd, head, msgsize + 1);
                if (r < msgsize + 1)
                {
                    printf("data too short\n");
                }
                else
                {
                    if (head[msgsize] == '#')
                    {
                        memcpy(&roomno, head, msgsize);
                        roomno = ntohl(roomno);

                        std::shared_ptr<Pool> pool;
                        {
                            std::lock_guard<std::mutex> lk(g_roomsMutex);
                            auto it = g_rooms.find(roomno);
                            if (it != g_rooms.end())
                            {
                                pool = it->second;
                            }
                        }

                        NetMsg msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = JOIN_MEETING_RESPONSE;
                        msg.len = sizeof(uint32_t);

                        if (!pool)
                        {
                            msg.ptr = (char *)malloc(msg.len);
                            uint32_t fail = htonl(0);
                            memcpy(msg.ptr, &fail, sizeof(uint32_t));
                            writetofd(connfd, msg);
                        }
                        else
                        {
                            bool ok = false;
                            {
                                std::lock_guard<std::mutex> lk(pool->lock);
                                if (pool->status == CLOSE || pool->num >= 1024)
                                {
                                    msg.ptr = (char *)malloc(msg.len);
                                    uint32_t full = htonl((uint32_t)-1);
                                    memcpy(msg.ptr, &full, sizeof(uint32_t));
                                    writetofd(connfd, msg);
                                }
                                else
                                {
                                    ok = true;
                                }
                            }
                            if (ok)
                            {
                                {
                                    std::lock_guard<std::mutex> qlock(pool->queueMutex);
                                    ClientConn cc;
                                    cc.cmd = 'J';
                                    cc.sockfd = connfd;
                                    pool->clientQueue.push(cc);
                                }
                                pool->queueCond.notify_one();

                                msg.ptr = (char *)malloc(msg.len);
                                uint32_t roomno_be = htonl(roomno);
                                memcpy(msg.ptr, &roomno_be, sizeof(uint32_t));
                                writetofd(connfd, msg);
                                return;
                            }
                        }
                    }
                    else
                    {
                        printf("format error\n");
                    }
                }
            }
            else
            {
                printf("data format error\n");
            }
        }
    }
}

void writetofd(SOCKET fd, NetMsg msg)
{
    char *buf = (char *)malloc(100);
    memset(buf, 0, 100);
    int bytestowrite = 0;
    buf[bytestowrite++] = '$';

    uint16_t type = msg.msgType;
    type = htons(type);
    memcpy(buf + bytestowrite, &type, sizeof(uint16_t));
    bytestowrite += 2;
    bytestowrite += 4; // skip ip
    uint32_t size = msg.len;
    size = htonl(size);
    memcpy(buf + bytestowrite, &size, sizeof(uint32_t));
    bytestowrite += 4;
    memcpy(buf + bytestowrite, msg.ptr, msg.len);
    bytestowrite += msg.len;
    buf[bytestowrite++] = '#';

    if (writen(fd, buf, bytestowrite) < bytestowrite)
    {
        printf("write fail\n");
    }

    if (msg.ptr)
    {
        free(msg.ptr);
        msg.ptr = NULL;
    }
    free(buf);
}
