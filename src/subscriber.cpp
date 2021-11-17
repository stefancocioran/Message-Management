#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "helpers.h"

void usage(char *file) {
    fprintf(stderr, "Usage: %s id_client server_address server_port\n", file);
    exit(0);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int sockfd, n, ret, flag = 1;
    struct sockaddr_in serv_addr;
    char buffer[BUFLEN_CLI];
    fd_set read_fds;
    fd_set tmp_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    if (argc < 4) {
        usage(argv[0]);
    }

    if (strlen(argv[1]) > 10) {
        fprintf(stderr, "Client ID should have maximum 10 characters\n");
        exit(0);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[3]));
    ret = inet_aton(argv[2], &serv_addr.sin_addr);
    DIE(ret == 0, "inet_aton");

    ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(ret < 0, "connect");

    FD_SET(sockfd, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    // se trimite ID-ul clientului TCP
    n = send(sockfd, argv[1], strlen(argv[1]) + 1, 0);
    DIE(n < 0, "send");

    // se dezactiveaza algoritmul lui Nagle
    ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag,
                     sizeof(int));
    DIE(ret < 0, "setsockopt");

    while (1) {
        tmp_fds = read_fds;
        ret = select(sockfd + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select");

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            memset(buffer, 0, BUFLEN_CLI);
            fgets(buffer, COMMAND_LEN - 1, stdin);

            char *buffer_copy = strdup(buffer);
            char *token;
            token = strtok(buffer, DELIM);

            bool subscribe = true;
            bool invalid = false;

            if (strcmp(token, "subscribe") == 0) {
                token = strtok(NULL, DELIM);
                if (token == NULL) {
                    fprintf(stderr, "Topic is missing!\n");
                    invalid = true;
                }

                token = strtok(NULL, DELIM);
                if (token == NULL) {
                    fprintf(stderr, "SF is missing!\n");
                    invalid = true;
                }

                int SF = atoi(token);
                if (SF != 0 && SF != 1) {
                    fprintf(stderr, "Invalid SF value!\n");
                    invalid = true;
                }

                token = strtok(NULL, DELIM);
                if (token != NULL) {
                    fprintf(stderr,
                            "Too many arguments in subscribe command!\n");
                    invalid = true;
                }
            } else if (strcmp(token, "unsubscribe") == 0) {
                subscribe = false;
                token = strtok(NULL, DELIM);
                if (token == NULL) {
                    fprintf(stderr, "Topic is missing!\n");
                    invalid = true;
                }

                token = strtok(NULL, DELIM);
                if (token != NULL) {
                    fprintf(stderr,
                            "Too many arguments in unsubscribe command!\n");
                    invalid = true;
                }
            } else if (strcmp(buffer, EXIT_SIGNAL) == 0) {
                break;
            } else {
                fprintf(stderr, "Invalid command!\n");
                invalid = true;
            }

            if (!invalid) {
                // se trimite mesaj la server
                n = send(sockfd, buffer_copy, strlen(buffer_copy), 0);
                DIE(n < 0, "send");

                if (subscribe) {
                    printf("Subscribed to topic.\n");
                } else {
                    printf("Unsubscribed from topic.\n");
                }
            }

            free(buffer_copy);
        }

        if (FD_ISSET(sockfd, &tmp_fds)) {
            // se primeste mesaj de la server
            memset(buffer, 0, BUFLEN_CLI);
            n = recv(sockfd, buffer, BUFLEN_CLI, 0);
            DIE(n < 0, "recv");

            if (strcmp(buffer, EXIT_SIGNAL) == 0) {
                break;
            }

            fwd_msg *received = (fwd_msg *)buffer;
            printf("%s:%d - %s - %s - %s\n", received->ip, received->port,
                   received->topic, received->data_type, received->content);
        }
    }

    close(sockfd);

    return 0;
}
