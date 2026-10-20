#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define PORT 8080
#define PAYLOAD_SIZE 1024

typedef struct {
    unsigned int seq;
    char data[PAYLOAD_SIZE];
} Frame;

int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        p += n;
        len -= (size_t)n;
    }

    return 0;
}

int recv_all(int fd, void *buf, size_t len)
{
    char *p = buf;
    size_t received = 0;

    while (received < len) {
        ssize_t n = recv(fd, p + received,
                         len - received, 0);

        if (n == 0)
            return received == 0 ? 0 : -1;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        received += (size_t)n;
    }

    return 1;
}

const char *get_algorithm(const char *mode)
{
    if (strcmp(mode, "reno") == 0 ||
        strcmp(mode, "sack") == 0)
        return "reno";

    if (strcmp(mode, "cubic") == 0)
        return "cubic";

    return NULL;
}

int main(int argc, char *argv[])
{
    int server_fd, client_fd;
    struct sockaddr_in server;
    const char *algorithm;

    if (argc != 2) {
        printf("Usage: %s <reno|sack|cubic>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "tahoe") == 0) {
        printf("Tahoe is not directly selectable through "
               "the standard Linux TCP socket API.\n");
        return 1;
    }

    algorithm = get_algorithm(argv[1]);

    if (algorithm == NULL) {
        printf("Unknown profile.\n");
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    /* Choose the congestion-control algorithm. */
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_CONGESTION,
                   algorithm, strlen(algorithm) + 1) < 0) {
        perror("TCP_CONGESTION");
        close(server_fd);
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, (struct sockaddr *)&server,
             sizeof(server)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("TCP server listening on port %d\n", PORT);
    printf("Profile: %s | Congestion control: %s\n",
           argv[1], algorithm);

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    unsigned long long count = 0;
    Frame frame;

    while (1) {
        int rc = recv_all(client_fd, &frame, sizeof(frame));

        if (rc == 0)
            break;

        if (rc < 0) {
            perror("recv");
            break;
        }

        if (send_all(client_fd, &frame, sizeof(frame)) < 0) {
            perror("send");
            break;
        }

        count++;
    }

    printf("Frames echoed: %llu\n", count);

    close(client_fd);
    close(server_fd);

    return 0;
}
