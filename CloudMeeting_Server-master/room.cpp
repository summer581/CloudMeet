#include "unpthread.h"
#include "msg.h"
#include "unp.h"
#include <map>
#include <vector>
#include <algorithm>
#include <chrono>

/* global room map: roomId -> shared_ptr<Pool> */
std::map<uint32_t, std::shared_ptr<Pool>> g_rooms;
std::mutex g_roomsMutex;

void room_thread_main(std::shared_ptr<Pool> user_pool)
{
    printf("room %u starting\n", user_pool->roomId);

    void accept_fd(std::shared_ptr<Pool>);
    void send_func(std::shared_ptr<Pool>);
    void fdclose(SOCKET fd, std::shared_ptr<Pool>);

    user_pool->acceptThr = std::thread(accept_fd, user_pool);

    for (int i = 0; i < SENDTHREADSIZE; i++)
    {
        user_pool->sendThreads.emplace_back(send_func, user_pool);
    }

    for (;;)
    {
        if (user_pool->shutdown.load())
            break;

        fd_set rset;
        {
            std::lock_guard<std::mutex> lk(user_pool->lock);
            rset = user_pool->fdset;
        }

        if (rset.fd_count == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int nsel;
        struct timeval time;
        memset(&time, 0, sizeof(struct timeval));
        nsel = Select(0, &rset, NULL, NULL, &time);
        if (nsel == 0) continue;

        std::vector<SOCKET> currentFds;
        {
            std::lock_guard<std::mutex> lk(user_pool->lock);
            currentFds = user_pool->activeFds;
        }

        for (auto fd : currentFds)
        {
            if (!FD_ISSET(fd, &rset))
                continue;

            char head[15] = {0};
            int ret = Readn(fd, head, 11);
            if (ret <= 0)
            {
                printf("peer close\n");
                fdclose(fd, user_pool);
            }
            else if (ret == 11)
            {
                if (head[0] == '$')
                {
                    MSG_TYPE msgtype;
                    memcpy(&msgtype, head + 1, 2);
                    msgtype = (MSG_TYPE)ntohs(msgtype);

                    NetMsg msg;
                    memset(&msg, 0, sizeof(NetMsg));
                    msg.targetfd = fd;
                    uint32_t ip_be;
                    memcpy(&ip_be, head + 3, 4);
                    msg.ip = ntohl(ip_be);
                    int msglen;
                    memcpy(&msglen, head + 7, 4);
                    msg.len = ntohl(msglen);

                    if (msgtype == IMG_SEND || msgtype == AUDIO_SEND || msgtype == TEXT_SEND)
                    {
                        msg.msgType = (msgtype == IMG_SEND) ? IMG_RECV : ((msgtype == AUDIO_SEND) ? AUDIO_RECV : TEXT_RECV);
                        msg.ptr = (char *)malloc(msg.len);
                        {
                            std::lock_guard<std::mutex> lk(user_pool->lock);
                            msg.ip = user_pool->fdToIp[fd];
                        }
                        if ((ret = Readn(fd, msg.ptr, msg.len)) < msg.len)
                        {
                            err_msg("3 msg format error");
                            if (msg.ptr) { free(msg.ptr); msg.ptr = NULL; }
                            fdclose(fd, user_pool);
                        }
                        else
                        {
                            int tail;
                            Readn(fd, &tail, 1);
                            if (tail != '#')
                            {
                                err_msg("4 msg format error");
                                if (msg.ptr) { free(msg.ptr); msg.ptr = NULL; }
                                fdclose(fd, user_pool);
                            }
                            else
                            {
                                user_pool->sendqueue.push_msg(msg);
                            }
                        }
                    }
                    else if (msgtype == CLOSE_CAMERA)
                    {
                        char tail;
                        Readn(fd, &tail, 1);
                        if (tail == '#' && msg.len == 0)
                        {
                            msg.msgType = CLOSE_CAMERA;
                            user_pool->sendqueue.push_msg(msg);
                        }
                        else
                        {
                            err_msg("camera data error ");
                            fdclose(fd, user_pool);
                        }
                    }
                    else
                    {
                        err_msg("unknown msg type %d from fd %llu", msgtype, (unsigned long long)fd);
                        if (msg.len > 0)
                        {
                            char *discard = (char *)malloc(msg.len + 1);
                            if (discard)
                            {
                                Readn(fd, discard, msg.len + 1);
                                free(discard);
                            }
                        }
                        else
                        {
                            char tail;
                            Readn(fd, &tail, 1);
                        }
                        fdclose(fd, user_pool);
                    }
                }
                else
                {
                    err_msg("1 msg format error: head[0]=0x%02x fd=%llu", (unsigned char)head[0], (unsigned long long)fd);
                    fdclose(fd, user_pool);
                }
            }
            else
            {
                err_msg("2 msg format error");
            }
            if (--nsel <= 0)
                break;
        }
    }

    /* shutdown sequence: wake accept thread */
    {
        std::lock_guard<std::mutex> lk(user_pool->queueMutex);
        user_pool->clientQueue.push({'X', INVALID_SOCKET});
    }
    user_pool->queueCond.notify_one();
    if (user_pool->acceptThr.joinable())
        user_pool->acceptThr.join();

    /* wake send threads */
    user_pool->sendqueue.shutdown();
    for (auto &t : user_pool->sendThreads)
    {
        if (t.joinable())
            t.join();
    }

    /* remove from global map; shared_ptr will auto-release when last reference drops */
    {
        std::lock_guard<std::mutex> lk(g_roomsMutex);
        g_rooms.erase(user_pool->roomId);
    }

    printf("room %u destroyed\n", user_pool->roomId);
}

void fdclose(SOCKET fd, std::shared_ptr<Pool> user_pool)
{
    if (user_pool->owner == fd)
    {
        user_pool->shutdown = true;
        user_pool->clear_room();
        printf("clear room %u\n", user_pool->roomId);
    }
    else
    {
        uint32_t ip = 0;
        {
            std::lock_guard<std::mutex> lk(user_pool->lock);
            auto it_ip = user_pool->fdToIp.find(fd);
            if (it_ip != user_pool->fdToIp.end())
            {
                ip = it_ip->second;
                user_pool->fdToIp.erase(it_ip);
            }
            FD_CLR(fd, &user_pool->fdset);
            user_pool->num--;
            auto it = std::find(user_pool->activeFds.begin(), user_pool->activeFds.end(), fd);
            if (it != user_pool->activeFds.end())
                user_pool->activeFds.erase(it);
        }

        NetMsg msg;
        memset(&msg, 0, sizeof(NetMsg));
        msg.msgType = PARTNER_EXIT;
        msg.targetfd = INVALID_SOCKET;
        msg.ip = ip;
        Close(fd);
        user_pool->sendqueue.push_msg(msg);
    }
}

void accept_fd(std::shared_ptr<Pool> user_pool)
{
    for (;;)
    {
        ClientConn cc;
        {
            std::unique_lock<std::mutex> lk(user_pool->queueMutex);
            while (user_pool->clientQueue.empty())
            {
                user_pool->queueCond.wait(lk);
            }
            cc = user_pool->clientQueue.front();
            user_pool->clientQueue.pop();
        }

        if (cc.cmd == 'X')
            break;

        SOCKET tfd = cc.sockfd;
        if (cc.cmd == 'C')
        {
            {
                std::lock_guard<std::mutex> lk(user_pool->lock);
                FD_SET(tfd, &user_pool->fdset);
                user_pool->owner = tfd;
                user_pool->fdToIp[tfd] = getpeerip(tfd);
                user_pool->num++;
                user_pool->activeFds.push_back(tfd);
                user_pool->status = ON;
            }

            uint32_t roomNo = htonl(user_pool->roomId);
            char *roomPtr = (char *)malloc(sizeof(uint32_t));
            memcpy(roomPtr, &roomNo, sizeof(uint32_t));
            NetMsg msg(CREATE_MEETING_RESPONSE, roomPtr, sizeof(uint32_t), tfd);
            user_pool->sendqueue.push_msg(msg);
            printf("send CREATE_MEETING_RESPONSE room %u to fd %llu\n", user_pool->roomId, (unsigned long long)tfd);
        }
        else if (cc.cmd == 'J')
        {
            uint32_t peer_ip = 0;
            {
                std::lock_guard<std::mutex> lk(user_pool->lock);
                if (user_pool->status == CLOSE)
                {
                    Close(tfd);
                    continue;
                }
                FD_SET(tfd, &user_pool->fdset);
                user_pool->num++;
                user_pool->activeFds.push_back(tfd);
                user_pool->fdToIp[tfd] = getpeerip(tfd);
                peer_ip = user_pool->fdToIp[tfd];
            }

            NetMsg msg;
            memset(&msg, 0, sizeof(NetMsg));
            msg.msgType = PARTNER_JOIN;
            msg.ptr = NULL;
            msg.len = 0;
            msg.targetfd = tfd;
            msg.ip = peer_ip;
            user_pool->sendqueue.push_msg(msg);

            NetMsg msg1;
            memset(&msg1, 0, sizeof(NetMsg));
            msg1.msgType = PARTNER_JOIN2;
            msg1.targetfd = tfd;

            std::vector<uint32_t> ips;
            {
                std::lock_guard<std::mutex> lk(user_pool->lock);
                for (auto s : user_pool->activeFds)
                {
                    if (s != tfd)
                    {
                        ips.push_back(user_pool->fdToIp[s]);
                    }
                }
            }

            msg1.len = (int)(ips.size() * sizeof(uint32_t));
            msg1.ptr = (char *)malloc(msg1.len);
            int pos = 0;
            for (auto ip : ips)
            {
                uint32_t ip_be = htonl(ip);
                memcpy(msg1.ptr + pos, &ip_be, sizeof(uint32_t));
                pos += sizeof(uint32_t);
            }
            user_pool->sendqueue.push_msg(msg1);
            printf("join meeting: %u\n", msg.ip);
        }
    }
}

void send_func(std::shared_ptr<Pool> user_pool)
{
    char *sendbuf = (char *)malloc(4 * MB);

    for (;;)
    {
        memset(sendbuf, 0, 4 * MB);
        NetMsg msg = user_pool->sendqueue.pop_msg();

        if (msg.msgType == (MSG_TYPE)0xFFFF)
        {
            if (msg.ptr)
            {
                free(msg.ptr);
                msg.ptr = NULL;
            }
            break;
        }

        int len = 0;

        sendbuf[len++] = '$';
        short type = htons((short)msg.msgType);
        memcpy(sendbuf + len, &type, sizeof(short));
        len += 2;

        if (msg.msgType == CREATE_MEETING_RESPONSE || msg.msgType == PARTNER_JOIN2)
        {
            memset(sendbuf + len, 0, 4);
            len += 4;
        }
        else if (msg.msgType == TEXT_RECV || msg.msgType == PARTNER_EXIT || msg.msgType == PARTNER_JOIN || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == CLOSE_CAMERA)
        {
            uint32_t ip_be = htonl(msg.ip);
            memcpy(sendbuf + len, &ip_be, sizeof(uint32_t));
            len += 4;
        }

        int msglen = htonl(msg.len);
        if (msg.msgType == CREATE_MEETING_RESPONSE && msg.len != (int)sizeof(uint32_t))
        {
            printf("BUG: CREATE_MEETING_RESPONSE msg.len=%d, forcing to 4\n", msg.len);
            msglen = htonl(sizeof(uint32_t));
        }
        memcpy(sendbuf + len, &msglen, sizeof(int));
        len += 4;
        if (msg.len > 0 && msg.ptr != NULL)
        {
            memcpy(sendbuf + len, msg.ptr, msg.len);
            len += msg.len;
        }
        sendbuf[len++] = '#';
        if (msg.msgType == CREATE_MEETING_RESPONSE)
        {
            printf("send_func CREATE_MEETING_RESPONSE: msg.len=%d msglen=0x%08X sendbuf[7:11]=%02X %02X %02X %02X total_len=%d\n",
                   msg.len, (unsigned)msglen,
                   (unsigned char)sendbuf[7], (unsigned char)sendbuf[8],
                   (unsigned char)sendbuf[9], (unsigned char)sendbuf[10], len);
        }

        std::vector<SOCKET> targets;
        {
            std::lock_guard<std::mutex> lk(user_pool->lock);
            if (msg.msgType == CREATE_MEETING_RESPONSE)
            {
                targets.push_back(msg.targetfd);
            }
            else if (msg.msgType == PARTNER_EXIT || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == TEXT_RECV || msg.msgType == CLOSE_CAMERA)
            {
                for (auto s : user_pool->activeFds)
                {
                    if (s != msg.targetfd)
                        targets.push_back(s);
                }
            }
            else if (msg.msgType == PARTNER_JOIN)
            {
                for (auto s : user_pool->activeFds)
                {
                    if (s != msg.targetfd)
                        targets.push_back(s);
                }
            }
            else if (msg.msgType == PARTNER_JOIN2)
            {
                for (auto s : user_pool->activeFds)
                {
                    if (s == msg.targetfd)
                        targets.push_back(s);
                }
            }
        }

        for (auto s : targets)
        {
            if (writen(s, sendbuf, len) < 0)
            {
                err_msg("writen error");
            }
        }

        if (msg.ptr)
        {
            free(msg.ptr);
            msg.ptr = NULL;
        }
    }
    free(sendbuf);
}
