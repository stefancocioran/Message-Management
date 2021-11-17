#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Macro de verificare a erorilor
 * Exemplu:
 *     int fd = open(file_name, O_RDONLY);
 *     DIE(fd == -1, "open failed");
 */
#define DIE(assertion, call_description)                       \
    do {                                                       \
        if (assertion) {                                       \
            fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__); \
            perror(call_description);                          \
            exit(EXIT_FAILURE);                                \
        }                                                      \
    } while (0)

#define BUFLEN_SERV sizeof(udp_msg)
#define BUFLEN_CLI sizeof(fwd_msg)
#define MAX_CLIENTS INT_MAX
#define DELIM " \n"
#define COMMAND_LEN 65
#define EXIT_SIGNAL "exit"
#define EXIT_SERVER "exit\n"

struct udp_msg {
    char topic[50];
    uint8_t data_type;
    char content[1501];
} __attribute__((packed));

struct fwd_msg {
    char ip[16];
    uint16_t port;
    char topic[51];
    char data_type[11];
    char content[1501];
} __attribute__((packed));

#endif
