
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main()
{
    int sock;
    struct sockaddr_in server, client;
    socklen_t len = sizeof(client);
    char buffer[1024];

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    server.sin_family = AF_INET;
    server.sin_port = htons(8081);
    server.sin_addr.s_addr = INADDR_ANY;

    bind(sock, (struct sockaddr *)&server, sizeof(server));

    printf("UDP Server waiting...\n");

    while (1)
    {
        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&client, &len);

        if (n < 0)
            continue;

        buffer[n] = '\0';
        printf("Received: %s\n", buffer);

        sendto(sock, "ACK", 3, 0,
               (struct sockaddr *)&client, len);
    }

    close(sock);
    return 0;
}
