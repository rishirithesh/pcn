#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define PORT 8080
#define PAYLOAD_SIZE 1024
#define MAX_FRAMES 100000

typedef struct {
    unsigned int seq;
    char data[PAYLOAD_SIZE];
} Frame;

typedef struct {
    int sock;
    int total;

    struct timespec *sent_at;
    int *valid;
    int *seen;
    pthread_mutex_t mutex;

    unsigned long long received;
    unsigned long long samples;
    double rtt_sum, rtt_min, rtt_max;
    struct timespec end_time;
    int error;
} Context;

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

double elapsed_ms(struct timespec end, struct timespec start)
{
    return (end.tv_sec - start.tv_sec) * 1000.0
         + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

double elapsed_sec(struct timespec end, struct timespec start)
{
    return (end.tv_sec - start.tv_sec)
         + (end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

void *receive_frames(void *arg)
{
    Context *ctx = arg;

    while (1) {
        Frame frame;
        int rc = recv_all(ctx->sock, &frame, sizeof(frame));

        if (rc == 0)
            break;

        if (rc < 0) {
            perror("recv");
            ctx->error = 1;
            break;
        }

        struct timespec now, sent;
        clock_gettime(CLOCK_MONOTONIC, &now);

        unsigned int seq = ntohl(frame.seq);

        if (seq >= (unsigned int)ctx->total)
            continue;

        pthread_mutex_lock(&ctx->mutex);

        int valid = ctx->valid[seq];
        sent = ctx->sent_at[seq];

        pthread_mutex_unlock(&ctx->mutex);

        if (!valid || ctx->seen[seq])
            continue;

        ctx->seen[seq] = 1;
        double rtt = elapsed_ms(now, sent);

        ctx->received++;
        ctx->samples++;
        ctx->rtt_sum += rtt;

        if (ctx->samples == 1 || rtt < ctx->rtt_min)
            ctx->rtt_min = rtt;

        if (ctx->samples == 1 || rtt > ctx->rtt_max)
            ctx->rtt_max = rtt;
    }

    clock_gettime(CLOCK_MONOTONIC, &ctx->end_time);
    return NULL;
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
    if (argc < 3 || argc > 4) {
        printf("Usage: %s <server-ip> <reno|sack|cubic> "
               "[frame-count]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[2], "tahoe") == 0) {
        printf("Tahoe is not directly selectable through "
               "the standard Linux TCP socket API.\n");
        return 1;
    }

    const char *algorithm = get_algorithm(argv[2]);

    if (algorithm == NULL) {
        printf("Unknown profile.\n");
        return 1;
    }

    int total = 10000;

    if (argc == 4) {
        char *end;
        long value = strtol(argv[3], &end, 10);

        if (*argv[3] == '\0' || *end != '\0' ||
            value < 1 || value > MAX_FRAMES) {
            printf("Frame count must be between 1 and %d\n",
                   MAX_FRAMES);
            return 1;
        }

        total = (int)value;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    if (setsockopt(sock, IPPROTO_TCP, TCP_CONGESTION,
                   algorithm, strlen(algorithm) + 1) < 0) {
        perror("TCP_CONGESTION");
        close(sock);
        return 1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (inet_pton(AF_INET, argv[1],
                  &server.sin_addr) != 1) {
        printf("Invalid IPv4 address\n");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server,
                sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    Context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.sock = sock;
    ctx.total = total;

    ctx.sent_at = calloc(total, sizeof(*ctx.sent_at));
    ctx.valid = calloc(total, sizeof(*ctx.valid));
    ctx.seen = calloc(total, sizeof(*ctx.seen));

    if (!ctx.sent_at || !ctx.valid || !ctx.seen) {
        perror("calloc");
        free(ctx.sent_at);
        free(ctx.valid);
        free(ctx.seen);
        close(sock);
        return 1;
    }

    if (pthread_mutex_init(&ctx.mutex, NULL) != 0) {
        printf("Mutex initialization failed\n");
        free(ctx.sent_at);
        free(ctx.valid);
        free(ctx.seen);
        close(sock);
        return 1;
    }

    pthread_t receiver;

    if (pthread_create(&receiver, NULL,
                       receive_frames, &ctx) != 0) {
        printf("Could not create receiver thread\n");
        pthread_mutex_destroy(&ctx.mutex);
        free(ctx.sent_at);
        free(ctx.valid);
        free(ctx.seen);
        close(sock);
        return 1;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int sent = 0;

    for (int i = 0; i < total; i++) {
        Frame frame;

        frame.seq = htonl((unsigned int)i);
        memset(frame.data, 'A' + i % 26, sizeof(frame.data));

        pthread_mutex_lock(&ctx.mutex);

        clock_gettime(CLOCK_MONOTONIC, &ctx.sent_at[i]);
        ctx.valid[i] = 1;

        pthread_mutex_unlock(&ctx.mutex);

        if (send_all(sock, &frame, sizeof(frame)) < 0) {
            perror("send");
            break;
        }

        sent++;
    }

    shutdown(sock, SHUT_WR);
    pthread_join(receiver, NULL);

    double elapsed = elapsed_sec(ctx.end_time, start);
    double throughput = elapsed > 0
        ? ctx.received * (double)PAYLOAD_SIZE * 8
          / elapsed / 1000000.0
        : 0.0;

    double pdr = sent > 0
        ? ctx.received * 100.0 / sent
        : 0.0;

    printf("\n===== TCP RESULTS =====\n");
    printf("Profile                  : %s\n", argv[2]);
    printf("Congestion control       : %s\n", algorithm);
    printf("Frames sent              : %d\n", sent);
    printf("Unique frames received   : %llu\n", ctx.received);
    printf("Elapsed time             : %.3f seconds\n", elapsed);
    printf("Payload throughput       : %.3f Mbps\n", throughput);
    printf("Application PDR          : %.2f%%\n", pdr);

    if (ctx.samples > 0) {
        printf("Average application RTT  : %.3f ms\n",
               ctx.rtt_sum / ctx.samples);
        printf("Minimum application RTT  : %.3f ms\n",
               ctx.rtt_min);
        printf("Maximum application RTT  : %.3f ms\n",
               ctx.rtt_max);
    }

    struct tcp_info info;
    memset(&info, 0, sizeof(info));
    socklen_t info_len = sizeof(info);

    if (getsockopt(sock, IPPROTO_TCP, TCP_INFO,
                   &info, &info_len) == 0) {
        printf("Kernel smoothed RTT      : %.3f ms\n",
               info.tcpi_rtt / 1000.0);

        printf("Estimated one-way delay  : %.3f ms (RTT/2)\n",
               info.tcpi_rtt / 2000.0);

        printf("TCP retransmissions      : %u\n",
               info.tcpi_total_retrans);

#ifdef TCPI_OPT_SACK
        printf("SACK negotiated          : %s\n",
               (info.tcpi_options & TCPI_OPT_SACK)
                   ? "yes" : "no");
#endif
    } else {
        perror("TCP_INFO");
    }

    pthread_mutex_destroy(&ctx.mutex);
    free(ctx.sent_at);
    free(ctx.valid);
    free(ctx.seen);
    close(sock);

    return ctx.error ? 1 : 0;
}
