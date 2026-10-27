#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main()
{
    int s, c, a[3], sum, carry;
    struct sockaddr_in server;

    s = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = INADDR_ANY;

    bind(s, (struct sockaddr *)&server, sizeof(server));
    listen(s, 1);

    printf("Waiting for client...\n");

    c = accept(s, NULL, NULL);
    read(c, a, sizeof(a));

    sum = a[0] + a[1] + a[2];

    while (sum > 255)
    {
        carry = sum >> 8;
        sum = (sum & 255) + carry;
    }

    printf("Received data: %d %d\n", a[0], a[1]);
    printf("Received checksum: %d\n", a[2]);

    if (sum == 255)
        printf("No error detected.\n");
    else
        printf("Error detected.\n");

    close(c);
    close(s);

    return 0;
}
