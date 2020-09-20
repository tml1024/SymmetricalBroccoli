/* -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */

// Receive head tracker data and output to a CSV file

#include <cmath>
#include <cstdio>
#include <cstring>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

int main(int, char **)
{

    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in sa;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(4242);

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        perror("bind");
        return 1;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    double first_data_time = 0;
    
    bool starting = true;
    while (true) {
        double d[6];

        int n = recv(sock, reinterpret_cast<char *>(d), sizeof(d), 0);
        if (n == -1) {
            perror("recv");
            return 1;
        }
        gettimeofday(&tv, NULL);
        double now_time = tv.tv_sec + tv.tv_usec / 1000000.0;
        if (first_data_time == 0)
            first_data_time = now_time;

        // While we are starting, reject buffered data
        if (starting && now_time - first_data_time < 0.001)
            continue;

        starting = false;

        printf("%.3f,%.1f,%.1f,%.1f,%d,%d,%d\n", now_time - first_data_time, d[0], d[1], d[2], (int)round(d[3]), (int)round(d[4]), (int)round(d[5]));
        fflush(stdout);
    }
}
    
    
