
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main()
{
    int sock;
    struct sockaddr_in server, client;
    socklen_t len;
    char buffer[1024];

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(8081);
    server.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&server,
             sizeof(server)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    printf("UDP Server waiting...\n");

    while (1) {
        len = sizeof(client);

        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&client, &len);

        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[n] = '\0';
        printf("Received: %s\n", buffer);

        if (sendto(sock, buffer, n, 0,
                   (struct sockaddr *)&client, len) < 0) {
            perror("sendto");
        }
    }

    close(sock);
    return 0;
}













#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

int main()
{
    int sock;
    struct sockaddr_in server;
    socklen_t len = sizeof(server);
    char input[800], message[1024];

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(8081);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    int seq = 1;

    while (1) {
        printf("Enter message (exit to quit): ");

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        if (strncmp(input, "exit", 4) == 0)
            break;

        snprintf(message, sizeof(message), "%d:%s", seq, input);

        int ack_received = 0;

        for (int attempt = 1; attempt <= 3; attempt++) {
            if (sendto(sock, message, strlen(message), 0,
                       (struct sockaddr *)&server, len) < 0) {
                perror("sendto");
                goto cleanup;
            }

            printf("Sent packet %d (attempt %d)\n", seq, attempt);

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);

            struct timeval timer;
            timer.tv_sec = 3;
            timer.tv_usec = 0;

            int ready = select(sock + 1, &readfds,
                               NULL, NULL, &timer);

            if (ready == 0) {
                printf("Timer expired! Retransmitting...\n");
                continue;
            }

            if (ready < 0) {
                perror("select");
                goto cleanup;
            }

            char ack[1024];
            struct sockaddr_in reply_addr;
            socklen_t reply_len = sizeof(reply_addr);

            int n = recvfrom(sock, ack, sizeof(ack) - 1, 0,
                             (struct sockaddr *)&reply_addr, &reply_len);

            if (n < 0) {
                perror("recvfrom");
                continue;
            }

            ack[n] = '\0';

            char expected[32];
            snprintf(expected, sizeof(expected), "%d:", seq);

            if (strncmp(ack, expected, strlen(expected)) == 0 &&
                n >= (int)strlen(expected) &&
                strcmp(ack + strlen(expected), input) == 0) {
                printf("ACK received for packet %d\n", seq);
                ack_received = 1;
                break;
            }

            printf("Unexpected response; waiting for correct ACK.\n");
            attempt--;
        }

        if (!ack_received)
            printf("Packet %d failed after 3 attempts\n", seq);

        seq++;
    }

cleanup:
    close(sock);
    return 0;
}
