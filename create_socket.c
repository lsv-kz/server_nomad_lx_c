#include "server.h"
#include <sys/un.h>

void set_sndbuf(int n);
//======================================================================
int create_server_socket(const struct Config *conf)
{
    int sockfd, sock_opt = 1;
    struct sockaddr_in server_sockaddr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sockfd == -1)
    {
        fprintf(stderr, "   Error socket(): %s\n", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt, sizeof(sock_opt)))
    {
        perror("setsockopt (SO_REUSEADDR)");
        close(sockfd);
        return -1;
    }
    
    if (conf->TcpNoDelay == 'y')
    {
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))
        {
            print_err("<%s:%d> setsockopt: unable to set TCP_NODELAY: %s\n", __func__, __LINE__, strerror(errno));
            close(sockfd);
            return -1;
        }
    }
    
    memset(&server_sockaddr, 0, sizeof server_sockaddr);
    server_sockaddr.sin_family = PF_INET;
    server_sockaddr.sin_port = htons(atoi(conf->servPort));
    if (inet_pton(PF_INET, conf->host, &(server_sockaddr.sin_addr)) < 1)
    {
        print_err("   Error inet_pton(%s): %s\n", conf->host, strerror(errno));
        close(sockfd);
        return -1;
    }
//  server_sockaddr.sin_addr.s_addr = inet_addr(addr);
//  server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sockfd, (struct sockaddr *) &server_sockaddr, sizeof (server_sockaddr)) == -1)
    {
        perror("server: bind");
        close(sockfd);
        return -1;
    }
    
    int sndbuf;
    socklen_t optlen = sizeof(sndbuf);
    getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen);
    print_err("<%s:%d> SO_SNDBUF: %d\n", __func__, __LINE__, sndbuf);
    
    if (listen(sockfd, conf->ListenBacklog) == -1)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}
//======================================================================
int create_client_socket(const char *host)
{
    int sockfd, n;//, sock_opt = 1;
    char addr[256];
    char port[16];
    
    if (!host) return -1;
    n = sscanf(host, "%[^:]:%s", addr, port);
    if(n == 2) //==== AF_INET ====
    {
        int sock_opt = 1;
        struct sockaddr_in sock_addr;
        
        memset(&sock_addr, 0, sizeof(sock_addr));

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(sockfd == -1)
        {
            return -errno;
        }
        
        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))  // SOL_TCP
        {
            print_err("<%s:%d> Error setsockopt(TCP_NODELAY): %s\n", __func__, __LINE__, strerror(errno));
            close(sockfd);
            return -1;
        }

        sock_addr.sin_port = htons(atoi(port));
        sock_addr.sin_family = AF_INET;
//      if (inet_aton(addr, &(sock_addr.sin_addr)) == 0)
        if (inet_pton(AF_INET, addr, &(sock_addr.sin_addr)) < 1)
        {
            fprintf(stderr, "   Error inet_pton(%s): %s\n", addr, strerror(errno));
            close(sockfd);
            return -errno;
        }

        if (connect(sockfd, (struct sockaddr *)(&sock_addr), sizeof(sock_addr)) != 0)
        {
            close(sockfd);
            return -errno;
        }
    }
    else //==== PF_UNIX ====
    {
        struct sockaddr_un sock_addr;
        sockfd = socket (PF_UNIX, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            return -errno;
        }
        
        sock_addr.sun_family = AF_UNIX;
        strcpy (sock_addr.sun_path, host);

        if (connect (sockfd, (struct sockaddr *) &sock_addr, SUN_LEN(&sock_addr)) == -1)
        {
            close(sockfd);
            return -errno;
        }
    }
    
    int flags = fcntl(sockfd, F_GETFL);
    if (flags == -1)
    {
        print_err("<%s:%d> Error fcntl(, F_GETFL, ): %s\n", __func__, __LINE__, strerror(errno));
    }
    else
    {
        flags |= O_NONBLOCK;
        if (fcntl(sockfd, F_SETFL, flags) == -1)
        {
            print_err("<%s:%d> Error fcntl(, F_SETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        }
    }

    return sockfd;
}
//======================================================================
int unixBind(const char *path, int type)
{
    struct sockaddr_un addr;

    if (path == NULL || strlen(path) >= sizeof(addr.sun_path) - 1)
    {
        errno = EINVAL;
        return -1;
    }
    
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    if (strlen(path) < sizeof(addr.sun_path))
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    else
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    int sock = socket(AF_UNIX, type, 0);
    if (sock == -1)
        return -1;

    if (bind(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
    {
        close(sock);
        return -1;
    }

    return sock;
}
//======================================================================
int unixConnect(const char *path)
{
    int sock, savedErrno;
    struct sockaddr_un addr;

    if (path == NULL || strlen(path) >= sizeof(addr.sun_path) - 1)
    {
        errno = EINVAL;
        return -1;
    }
    
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    if (strlen(path) < sizeof(addr.sun_path))
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    else
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1)
        return -1;

    if (connect(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
    {
        savedErrno = errno;
        close(sock);
        errno = savedErrno;
        return -1;
    }

    return sock;
}
