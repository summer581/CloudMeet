#ifndef __unp_h
#define __unp_h

#define FD_SETSIZE 1024
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef _WIN64
typedef long long ssize_t;
#else
typedef long ssize_t;
#endif
#endif

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

#ifndef SHUT_RD
#define SHUT_RD     0
#define SHUT_WR     1
#define SHUT_RDWR   2
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN     16
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN    46
#endif

#define bzero(ptr, n) memset(ptr, 0, n)

#define LISTENQ     1024
#define MAXLINE     4096
#define MAXSOCKADDR 128
#define BUFFSIZE    8192

#define SERV_PORT        9877
#define SERV_PORT_STR   "9877"

#define SA  struct sockaddr

#define MIN(a,b)    ((a) < (b) ? (a) : (b))
#define MAX(a,b)    ((a) > (b) ? (a) : (b))

/* Socket wrappers used by the project */
SOCKET   Tcp_connect(const char *host, const char *serv);
SOCKET   Tcp_listen(const char *host, const char *service, socklen_t *addrlen);
SOCKET   Accept(SOCKET listenfd, SA *addr, socklen_t *addrlen);
int      Select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
ssize_t  Readn(SOCKET fd, void *buf, size_t size);
ssize_t  writen(SOCKET fd, const void *buf, size_t n);
void     Close(SOCKET fd);
void     Listen(SOCKET fd, int backlog);
void     Setsockopt(SOCKET fd, int level, int optname, const void *optval, socklen_t optlen);
char    *Sock_ntop(char *str, int size, const sockaddr *sa, socklen_t salen);
uint32_t getpeerip(SOCKET fd);

/* Memory wrapper */
void    *Calloc(size_t n, size_t size);

/* Error functions */
void     err_dump(const char *, ...);
void     err_msg(const char *, ...);
void     err_quit(const char *, ...);
void     err_ret(const char *, ...);
void     err_sys(const char *, ...);

#endif  /* __unp_h */
