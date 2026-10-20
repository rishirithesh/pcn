#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main()
{
    int sock;
    struct sockaddr_in server;
    struct hostent *host;
    char buffer[1024];

    // Get the server IP address
    host = gethostbyname("lms.ssn.edu.in");

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    // Configure server address
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    memcpy(&server.sin_addr, host->h_addr_list[0],
           host->h_length);

    // Connect to server
    connect(sock, (struct sockaddr *)&server, sizeof(server));

    // Send HTTP GET request
    char request[] =
        "GET / HTTP/1.0\r\n"
        "Host: lms.ssn.edu.in\r\n"
        "Connection: close\r\n\r\n";

    send(sock, request, strlen(request), 0);

    // Receive and display webpage response
    int n;

    while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0)
        write(STDOUT_FILENO, buffer, n);

    close(sock);

    return 0;
}
