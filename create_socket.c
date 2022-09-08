#include "server.h"
#include <sys/un.h>

//======================================================================
int create_server_socket(const Config *conf)
{
    int sockfd;
    struct addrinfo  hints, *result, *rp;
    int n;
    const int optval = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((n = getaddrinfo(conf->SERVER_ADDR, conf->SERVER_PORT, &hints, &result)) != 0) 
    {
        fprintf(stderr, "Error getaddrinfo(%s:%s): %s\n", conf->SERVER_ADDR, conf->SERVER_PORT, gai_strerror(n));
        return -1;
    }  

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sockfd);
    }

    if (rp == NULL) 
    {
        fprintf(stderr, "Error: failed to bind\n");
        return -1;
    }

    if (conf->tcp_nodelay == 'y')
    {
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&optval, sizeof(optval)); // SOL_TCP
    }

    int flags = fcntl(sockfd, F_GETFL);
    if (flags == -1)
    {
        fprintf(stderr, "Error fcntl(, F_GETFL, ): %s\n", strerror(errno));
    }
    else
    {
        flags |= O_NONBLOCK;
        if (fcntl(sockfd, F_SETFL, flags) == -1)
        {
            fprintf(stderr, "Error fcntl(, F_SETFL, ): %s\n", strerror(errno));
        }
    }

    freeaddrinfo(result);

    if (listen(sockfd, conf->LISTEN_BACKLOG) == -1) 
    {
        fprintf(stderr, "Error listen(): %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}
//======================================================================
int create_client_socket(const char *host)
{
    int sockfd, n;
    char addr[256];
    char port[16];

    if (!host) return -1;
    n = sscanf(host, "%[^:]:%s", addr, port);
    if(n == 2) //==== AF_INET ====
    {
        const int sock_opt = 1;
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
        snprintf(sock_addr.sun_path, sizeof(sock_addr.sun_path), "%s", host);

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

    if ((path == NULL) || (strlen(path) >= (sizeof(addr.sun_path) - 1)))
    {
        fprintf(stderr, "<%s:%d> Error: %s\n", __func__, __LINE__, strerror(EINVAL));
        errno = EINVAL;
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    int sock = socket(AF_UNIX, type, 0);
    if (sock == -1)
    {
        fprintf(stderr, "<%s:%d> Error socket(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    if (bind(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
    {
        fprintf(stderr, "<%s:%d> Error bind(): %s\n", __func__, __LINE__, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}
//======================================================================
int unixConnect(const char *path, int type)
{
    int sock;
    struct sockaddr_un addr;

    if ((path == NULL) || (strlen(path) >= (sizeof(addr.sun_path) - 1)))
    {
        fprintf(stderr, "<%s:%d> Error: %s\n", __func__, __LINE__, strerror(EINVAL));
        errno = EINVAL;
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    sock = socket(AF_UNIX, type, 0);
    if (sock == -1)
    {
        fprintf(stderr, "<%s:%d> Error socket(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    if (connect(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
    {
        fprintf(stderr, "<%s:%d> Error connect(): %s\n", __func__, __LINE__, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}
