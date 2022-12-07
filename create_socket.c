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

    if ((n = getaddrinfo(conf->ServerAddr, conf->ServerPort, &hints, &result)) != 0)
    {
        fprintf(stderr, "Error getaddrinfo(%s:%s): %s\n", conf->ServerAddr, conf->ServerPort, gai_strerror(n));
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

    if (conf->TcpNoDelay == 'y')
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

    if (listen(sockfd, conf->ListenBacklog) == -1)
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
    if (n == 2) //==== AF_INET ====
    {
        const int sock_opt = 1;
        struct sockaddr_in sock_addr;

        memset(&sock_addr, 0, sizeof(sock_addr));

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd == -1)
        {
            return -errno;
        }

        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))  // SOL_TCP
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
int get_sndbuf(int domain, int type, int protocol)
{
    int sock = socket(domain, type, protocol);
    if (sock < 0)
    {
        fprintf(stderr, "<%s:%d> Error socketpair(): %s\n", __func__, __LINE__, strerror(errno));
        return -errno;
    }

    int sndbuf;
    socklen_t optlen = sizeof(sndbuf);
    if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen) < 0)
    {
        fprintf(stderr, "<%s:%d> Error getsockopt(SO_SNDBUF): %s\n", __func__, __LINE__, strerror(errno));
        close(sock);
        return -errno;
    }

    close(sock);
    return sndbuf;
}
