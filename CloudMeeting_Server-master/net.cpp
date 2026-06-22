#include "unp.h"

uint32_t getpeerip(SOCKET fd)
{
    sockaddr_in peeraddr;
    int addrlen = sizeof(peeraddr);
    if(getpeername(fd, (sockaddr *)&peeraddr, &addrlen) < 0)
    {
        err_msg("getpeername error");
        return 0;
    }
    return ntohl(peeraddr.sin_addr.s_addr);
}

int Select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    int n;
    for(;;)
    {
        n = select(nfds, readfds, writefds, exceptfds, timeout);
        if(n < 0)
        {
            int err = WSAGetLastError();
            if(err == WSAEINTR) continue;
            else err_quit("select error, WSA error = %d", err);
        }
        else break;
    }
    return n;
}

ssize_t Readn(SOCKET fd, void *buf, size_t size)
{
    ssize_t lefttoread = size, hasread = 0;
    char *ptr = (char *)buf;
    while(lefttoread > 0)
    {
        hasread = recv(fd, ptr, (int)lefttoread, 0);
        if(hasread < 0)
        {
            int err = WSAGetLastError();
            if(err == WSAEINTR)
            {
                hasread = 0;
            }
            else
            {
                return -1;
            }
        }
        else if(hasread == 0)
        {
            break;
        }
        lefttoread -= hasread;
        ptr += hasread;
    }
    return size - lefttoread;
}

ssize_t writen(SOCKET fd, const void *buf, size_t n)
{
    ssize_t lefttowrite = n, haswrite = 0;
    char *ptr = (char *)buf;
    while(lefttowrite > 0)
    {
        haswrite = send(fd, ptr, (int)lefttowrite, 0);
        if(haswrite < 0)
        {
            int err = WSAGetLastError();
            if(err == WSAEINTR)
            {
                haswrite = 0;
            }
            else
            {
                return -1;
            }
        }
        lefttowrite -= haswrite;
        ptr += haswrite;
    }
    return n;
}

char *Sock_ntop(char *str, int size, const sockaddr *sa, socklen_t salen)
{
    switch (sa->sa_family)
    {
    case AF_INET:
        {
            struct sockaddr_in *sin = (struct sockaddr_in *)sa;
            if(inet_ntop(AF_INET, &sin->sin_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin_port) > 0)
            {
                snprintf(str + strlen(str), size - strlen(str), ":%d", ntohs(sin->sin_port));
            }
            return str;
        }
    case AF_INET6:
        {
            struct sockaddr_in6 *sin = (struct sockaddr_in6 *)sa;
            if(inet_ntop(AF_INET6, &sin->sin6_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin6_port) > 0)
            {
                snprintf(str + strlen(str), size - strlen(str), ":%d", ntohs(sin->sin6_port));
            }
            return str;
        }
    default:
        return NULL;
    }
    return NULL;
}

void Setsockopt(SOCKET fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if(setsockopt(fd, level, optname, (const char *)optval, optlen) < 0)
    {
        err_msg("setsockopt error");
    }
}

void Close(SOCKET fd)
{
    closesocket(fd);
}

void Listen(SOCKET fd, int backlog)
{
    if(listen(fd, backlog) < 0)
    {
        err_quit("listen error");
    }
}

SOCKET Tcp_connect(const char *host, const char *serv)
{
    SOCKET sockfd;
    int n;
    struct addrinfo hints, *res, *ressave;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((n = getaddrinfo(host, serv, &hints, &res)) != 0)
    {
        err_quit("tcp_connect error for %s, %s: %s", host, serv, gai_strerror(n));
    }

    ressave = res;
    do
    {
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sockfd == INVALID_SOCKET) continue;
        if(connect(sockfd, res->ai_addr, (int)res->ai_addrlen) == 0) break;
        closesocket(sockfd);
    } while((res = res->ai_next) != NULL);

    if(res == NULL)
    {
        err_quit("tcp_connect error for %s,%s", host, serv);
    }
    freeaddrinfo(ressave);
    return sockfd;
}

SOCKET Tcp_listen(const char *host, const char *service, socklen_t *addrlen)
{
    SOCKET listenfd;
    int n;
    const int on = 1;
    struct addrinfo hints, *res, *ressave;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char addr[MAXSOCKADDR];

    if((n = getaddrinfo(host, service, &hints, &res)) > 0)
    {
        err_quit("tcp listen error for %s %s: %s", host, service, gai_strerror(n));
    }
    ressave = res;
    do
    {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(listenfd == INVALID_SOCKET)
        {
            continue;
        }
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if(bind(listenfd, res->ai_addr, (int)res->ai_addrlen) == 0)
        {
            printf("server address: %s\n", Sock_ntop(addr, MAXSOCKADDR, res->ai_addr, res->ai_addrlen));
            break;
        }
        Close(listenfd);
    } while((res = res->ai_next) != NULL);
    freeaddrinfo(ressave);

    if(res == NULL)
    {
        err_quit("tcp listen error for %s: %s", host, service);
    }

    Listen(listenfd, LISTENQ);

    if(addrlen)
    {
        *addrlen = (socklen_t)res->ai_addrlen;
    }

    return listenfd;
}

SOCKET Accept(SOCKET listenfd, SA *addr, socklen_t *addrlen)
{
    SOCKET n;
    for(;;)
    {
        int len = addrlen ? (int)*addrlen : sizeof(SA);
        n = accept(listenfd, addr, &len);
        if(n == INVALID_SOCKET)
        {
            int err = WSAGetLastError();
            if(err == WSAEINTR)
                continue;
            else
                err_quit("accept error");
        }
        else
        {
            if(addrlen) *addrlen = (socklen_t)len;
            return n;
        }
    }
}
