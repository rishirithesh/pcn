
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

int main()
{
    int sock;
    struct sockaddr_in server;
    char buffer[1024];
    socklen_t len = sizeof(server);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    server.sin_family = AF_INET;
    server.sin_port = htons(8081);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));

    int seq = 1;

    while (1)
    {
        printf("Enter message (exit to quit): ");
        fgets(buffer, sizeof(buffer), stdin);

        if (strncmp(buffer, "exit", 4) == 0)
            break;

        char message[1024];
        snprintf(message, sizeof(message), "%d:%s", seq, buffer);

        int ack_received = 0;

        for (int attempt = 1; attempt <= 3; attempt++)
        {
            sendto(sock, message, strlen(message), 0,
                   (struct sockaddr *)&server, len);

            printf("Sent packet %d (attempt %d)\n",
                   seq, attempt);

            char ack[1024];
            int n = recvfrom(sock, ack, sizeof(ack) - 1, 0,
                             (struct sockaddr *)&server, &len);

            if (n > 0)
            {
                ack[n] = '\0';

                if (strcmp(ack, "ACK") == 0)
                {
                    printf("ACK received for packet %d\n", seq);
                    ack_received = 1;
                    break;
                }
            }

            printf("Timeout! Retransmitting...\n");
        }

        if (!ack_received)
            printf("Packet %d failed after 3 attempts\n", seq);

        seq++;
    }

    close(sock);
    return 0;
}
