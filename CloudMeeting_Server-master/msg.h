#ifndef MSG_H
#define MSG_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <memory>
#include "unp.h"
#include "netheader.h"

#define MAXSIZE 10000
#define MB (1024*1024)
#define SENDTHREADSIZE 5

enum STATUS
{
    CLOSE = 0,
    ON = 1,
};

struct NetMsg
{
    char    *ptr;
    int     len;
    SOCKET  targetfd;
    MSG_TYPE msgType;
    uint32_t ip;
    Image_Format format;

    NetMsg() : ptr(NULL), len(0), targetfd(INVALID_SOCKET), msgType((MSG_TYPE)0), ip(0), format((Image_Format)0) {}
    NetMsg(MSG_TYPE msg_type, char *msg, int length, SOCKET fd)
        : ptr(msg), len(length), targetfd(fd), msgType(msg_type), ip(0), format((Image_Format)0) {}
};

struct SEND_QUEUE
{
private:
    std::mutex              lock;
    std::condition_variable cond;
    std::queue<NetMsg>       send_queue;
    bool                     shutdown_flag = false;

public:
    void push_msg(NetMsg msg)
    {
        std::unique_lock<std::mutex> lk(lock);
        while(send_queue.size() >= MAXSIZE && !shutdown_flag)
        {
            cond.wait(lk);
        }
        if(shutdown_flag)
        {
            if(msg.ptr) { free(msg.ptr); msg.ptr = NULL; }
            return;
        }
        send_queue.push(msg);
        lk.unlock();
        cond.notify_one();
    }

    NetMsg pop_msg()
    {
        std::unique_lock<std::mutex> lk(lock);
        while(send_queue.empty() && !shutdown_flag)
        {
            cond.wait(lk);
        }
        if(send_queue.empty())
        {
            NetMsg empty;
            empty.msgType = (MSG_TYPE)0xFFFF; // poison pill
            return empty;
        }
        NetMsg msg = send_queue.front();
        send_queue.pop();
        lk.unlock();
        cond.notify_one();
        return msg;
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lk(lock);
        shutdown_flag = true;
        for(int i = 0; i < SENDTHREADSIZE; i++)
        {
            NetMsg msg;
            msg.msgType = (MSG_TYPE)0xFFFF;
            send_queue.push(msg);
        }
        cond.notify_all();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(lock);
        while(!send_queue.empty())
        {
            NetMsg msg = send_queue.front();
            send_queue.pop();
            if(msg.ptr) free(msg.ptr);
        }
    }
};

/* Client connection request passed from acceptor thread to room thread */
struct ClientConn {
    char cmd;       // 'C' = create meeting, 'J' = join meeting, 'X' = shutdown
    SOCKET sockfd;
};

struct Pool
{
    fd_set fdset;
    std::mutex lock;
    SOCKET owner;
    int num;
    std::map<SOCKET, uint32_t> fdToIp;
    std::vector<SOCKET> activeFds;
    uint32_t roomId;
    int status;
    SEND_QUEUE sendqueue;

    std::queue<ClientConn> clientQueue;
    std::mutex queueMutex;
    std::condition_variable queueCond;

    std::thread acceptThr;
    std::vector<std::thread> sendThreads;
    std::atomic<bool> shutdown{false};

    Pool(uint32_t rid) : owner(0), num(0), roomId(rid), status(ON)
    {
        FD_ZERO(&fdset);
    }

    void clear_room()
    {
        std::lock_guard<std::mutex> lk(lock);
        status = CLOSE;
        for(auto s : activeFds)
        {
            Close(s);
        }
        activeFds.clear();
        fdToIp.clear();
        num = 0;
        owner = 0;
        FD_ZERO(&fdset);
        sendqueue.clear();
    }
};

/* global room map: roomId -> shared_ptr<Pool> */
extern std::map<uint32_t, std::shared_ptr<Pool>> g_rooms;
extern std::mutex g_roomsMutex;

#endif // MSG_H
