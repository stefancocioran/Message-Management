#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "helpers.h"

using namespace std;

void usage(char *file) {
    fprintf(stderr, "Usage: %s server_port\n", file);
    exit(0);
}

/**
 * @brief Trimite mesaj tuturor clientilor TCP care sunt online si sunt
 * abonati la topicul pentru care se primesc informatii. Daca un client
 * este abonat cu SF si este offline, mesajul este stocat in "client_messages"
 *
 * @param clients_sock          contine socket-urile atribuite clientilor
 * @param client_online         retine care clienti sunt online/offline
 * @param client_subscriptions  contine abonamentele clientilor
 * @param message               mesajul de trimis
 * @param top                   topicul mesajului care trebuie trimis
 * @param client_messages       contine mesajele pentru abonamentele cu SF
 */
void directly_forward_message(
    unordered_map<string, int> clients_sock,
    unordered_map<string, bool> client_online,
    unordered_map<string, unordered_map<string, int>> client_subscriptions,
    fwd_msg message, char *top,
    unordered_map<string, vector<fwd_msg>> *client_messages) {
    string topic(top);
    for (auto &subs : client_subscriptions) {
        if (subs.second.find(topic) != subs.second.end()) {
            // daca clientul este online, se trimite mesajul direct
            if (client_online[subs.first] == true) {
                int n = send(clients_sock[subs.first], (char *)&message,
                             sizeof(message), 0);
                DIE(n == -1, "send");
            } else {
                // clientul nu este online, se verifica daca este abonat cu SF 1
                if (subs.second[topic] == 1) {
                    // este abonat cu SF 1, mesajul este pastrat
                    if (client_messages->find(subs.first) ==
                        client_messages->end()) {
                        vector<fwd_msg> new_msg;
                        new_msg.emplace_back(message);
                        client_messages->insert({subs.first, new_msg});
                    } else {
                        client_messages->at(subs.first).emplace_back(message);
                    }
                }
            }
        }
    }
}

/**
 * @brief Trimite in ordinea in care au fost primite mesajele stocate pentru
 * abonamentele cu SF ale unui client atunci cand acesta se reconecteaza la
 * server.
 *
 * @param client_id             ID-ul clientului la care trebuiesc trimise
 *                                 mesajele stocate pentru abonamentele cu SF
 * @param client_messages       contine mesajele pentru abonamentele cu SF
 * @param client_subscriptions  contine abonamentele clientilor
 * @param clients_sock          contine socket-urile atribuite clientilor
 */
void send_sf_messages(
    char *client_id, unordered_map<string, vector<fwd_msg>> *client_messages,
    unordered_map<string, unordered_map<string, int>> client_subscriptions,
    unordered_map<string, int> clients_sock) {
    string id(client_id);

    // se verifica daca clientul este abonat la topicul respectiv si daca sunt
    // mesaje care asteapta sa fie trimise
    if (client_subscriptions.find(id) == client_subscriptions.end() ||
        client_messages->find(id) == client_messages->end()) {
        return;
    }

    // se trimit catre client toate mesajele stocate apoi sunt sterse
    if (!client_messages->at(client_id).empty()) {
        for (auto &msg : client_messages->at(id)) {
            int n = send(clients_sock[id], (char *)&msg, sizeof(msg), 0);
            DIE(n == -1, "send");
        }
        client_messages->at(id).clear();
    }
}

/**
 * @brief Inchide conexiunea cu toti clientii atunci cand serverul primeste
 * de la tastatura comanda "exit"
 *
 * @param fdmax             valoarea maxima pentru file descriptor
 * @param read_fds          multimea de citire
 * @param tcp_sock          socket-ul pentru portul TCP
 * @param udp_sock          socket-ul pentru portul TCP
 */
void close_all_clients(int fdmax, fd_set read_fds, int tcp_sock, int udp_sock) {
    for (int j = 0; j <= fdmax; j++) {
        if (FD_ISSET(j, &read_fds) && j != STDIN_FILENO && j != tcp_sock &&
            j != udp_sock) {
            int n = send(j, EXIT_SIGNAL, strlen(EXIT_SIGNAL) + 1, 0);
            DIE(n == -1, "send");
            close(j);
        }
    }
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int tcp_sock, udp_sock, newsockfd, portno;
    int n, i, ret, flag = 1;
    char buffer[BUFLEN_SERV];
    struct sockaddr_in serv_addr_tcp, serv_addr_udp, cli_addr;
    socklen_t clilen;
    unordered_map<string, vector<fwd_msg>> client_messages;
    unordered_map<string, int> client_sock;
    unordered_map<string, bool> client_online;
    unordered_map<string, unordered_map<string, int>> client_subscriptions;

    fd_set read_fds;  // multimea de citire folosita in select()
    fd_set tmp_fds;   // multime folosita temporar
    int fdmax;        // valoare maxima fd din multimea read_fds

    if (argc < 2) {
        usage(argv[0]);
    }

    // se goleste multimea de descriptori de citire (read_fds) si multimea
    // temporara (tmp_fds)
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_sock < 0, "socket");

    /*Deschidere socket*/
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_sock < 0, "socket_udp");

    portno = atoi(argv[1]);
    DIE(portno == 0, "atoi");

    memset((char *)&serv_addr_tcp, 0, sizeof(serv_addr_tcp));
    serv_addr_tcp.sin_family = AF_INET;
    serv_addr_tcp.sin_port = htons(portno);
    serv_addr_tcp.sin_addr.s_addr = INADDR_ANY;

    memset((char *)&serv_addr_udp, 0, sizeof(serv_addr_udp));
    serv_addr_udp.sin_family = AF_INET;
    serv_addr_udp.sin_port = htons(portno);
    serv_addr_udp.sin_addr.s_addr = INADDR_ANY;

    ret = bind(tcp_sock, (struct sockaddr *)&serv_addr_tcp,
               sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    ret = bind(udp_sock, (struct sockaddr *)&serv_addr_udp,
               sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    ret = listen(tcp_sock, MAX_CLIENTS);
    DIE(ret < 0, "listen");

    // se adauga noul file descriptor (socketul pe care se asculta conexiuni) in
    // multimea read_fds
    FD_SET(tcp_sock, &read_fds);
    FD_SET(udp_sock, &read_fds);
    fdmax = max(tcp_sock, udp_sock);

    // se adauga stdin in multime
    FD_SET(STDIN_FILENO, &read_fds);

    bool forever = true;
    while (forever) {
        tmp_fds = read_fds;

        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select");

        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &tmp_fds)) {
                if (i == tcp_sock) {
                    // a venit o cerere de conexiune pe socketul inactiv (cel cu
                    // listen), pe care serverul o accepta
                    clilen = sizeof(cli_addr);
                    newsockfd =
                        accept(tcp_sock, (struct sockaddr *)&cli_addr, &clilen);
                    DIE(newsockfd < 0, "accept");

                    // se dezactiveaza algoritmul lui Nagle
                    ret = setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY,
                                     (char *)&flag, sizeof(int));
                    DIE(ret < 0, "setsockopt");

                    // se adauga noul socket intors de accept() la multimea
                    // descriptorilor de citire
                    FD_SET(newsockfd, &read_fds);

                    fdmax = max(newsockfd, fdmax);

                    // se primeste ID-ul clientului TCP
                    memset(buffer, 0, BUFLEN_SERV);
                    n = recv(newsockfd, buffer, BUFLEN_SERV, 0);
                    DIE(n < 0, "recv");

                    unordered_map<string, bool>::iterator it =
                        client_online.find(string(buffer));
                    // se verifica daca clientul este conectat
                    if (it != client_online.end() && it->second == true) {
                        printf("Client %s already connected.\n", buffer);

                        int n = send(newsockfd, EXIT_SIGNAL,
                                     strlen(EXIT_SIGNAL) + 1, 0);
                        DIE(n == -1, "send");

                        // se inchide socket-ul si este scos din multimea de
                        // citire
                        close(newsockfd);
                        FD_CLR(newsockfd, &read_fds);
                    } else {
                        client_online[string(buffer)] = true;
                        client_sock[string(buffer)] = newsockfd;

                        printf("New client %s connected from %s:%d.\n", buffer,
                               inet_ntoa(cli_addr.sin_addr),
                               ntohs(cli_addr.sin_port));

                        // dupa reconectare se trimit clientului mesajele pentru
                        // topicurile la care este abonat cu SF
                        send_sf_messages(buffer, &client_messages,
                                         client_subscriptions, client_sock);
                    }
                } else if (i == STDIN_FILENO) {
                    // daca serverul primeste comanda "exit", se inchid
                    // conexiunile cu toti clientii se inchide serverul

                    memset(buffer, 0, BUFLEN_SERV);
                    fgets(buffer, BUFLEN_SERV - 1, stdin);

                    if (strcmp(buffer, EXIT_SERVER) == 0) {
                        // se iese din bucla "while" si urmeaza sa se inchida
                        // conexiunea cu toti clientii TCP
                        forever = false;
                        break;
                    } else {
                        fprintf(stderr,
                                "The only accepted command is \"exit\".\n");
                    }
                } else if (i == udp_sock) {
                    // s-au primit date pe unul din socketii de client udp,
                    // asa ca serverul trebuie sa le receptioneze

                    memset(buffer, 0, BUFLEN_SERV);
                    n = recv(i, buffer, BUFLEN_SERV, 0);
                    DIE(n < 0, "recv");

                    udp_msg *udp_message = (udp_msg *)buffer;
                    fwd_msg forward_message;

                    strcpy(forward_message.ip,
                           inet_ntoa(serv_addr_udp.sin_addr));
                    forward_message.port = ntohs(serv_addr_udp.sin_port);
                    strcpy(forward_message.topic, udp_message->topic);

                    bool forward = true;
                    switch (udp_message->data_type) {
                        case 0: {
                            long long result;
                            result =
                                ntohl(*(uint32_t *)(udp_message->content + 1));

                            if (udp_message->content[0]) {
                                result *= -1;
                            }

                            sprintf(forward_message.content, "%lld", result);
                            strcpy(forward_message.data_type, "INT");
                        } break;

                        case 1: {
                            double result;
                            result = ntohs(*(uint16_t *)(udp_message->content));
                            result /= 100;

                            sprintf(forward_message.content, "%.2lf", result);
                            strcpy(forward_message.data_type, "SHORT_REAL");
                        } break;

                        case 2: {
                            float result;
                            result =
                                ntohl(*(uint32_t *)(udp_message->content + 1));
                            result /= pow(10, udp_message->content[5]);

                            if (udp_message->content[0]) {
                                result *= -1;
                            }

                            sprintf(forward_message.content, "%f", result);
                            strcpy(forward_message.data_type, "FLOAT");
                        } break;

                        case 3:
                            strcpy(forward_message.data_type, "STRING");
                            strcpy(forward_message.content,
                                   udp_message->content);
                            break;

                        default:
                            forward = false;
                            fprintf(stderr, "Invalid data type!\n");
                            break;
                    }

                    if (forward) {
                        directly_forward_message(
                            client_sock, client_online, client_subscriptions,
                            forward_message, udp_message->topic,
                            &client_messages);
                    }

                } else {
                    // s-au primit date pe unul din socketii de client tcp,
                    // asa ca serverul trebuie sa le receptioneze
                    memset(buffer, 0, BUFLEN_SERV);
                    n = recv(i, buffer, sizeof(buffer), 0);
                    DIE(n < 0, "recv");

                    // se identifica id-ul clientului de la care se primesc date
                    string client_id;
                    for (auto &elem : client_sock) {
                        if (elem.second == i) {
                            client_id = elem.first;
                            break;
                        }
                    }

                    if (n == 0) {
                        // conexiunea s-a inchis
                        printf("Client %s disconnected.\n", client_id.c_str());

                        // se seteaza clientul ca fiind offline
                        client_online[client_id] = false;
                        client_sock.erase(client_id);

                        // se inchide socket-ul si este scos din multimea de
                        // citire
                        close(i);
                        FD_CLR(i, &read_fds);
                    } else {
                        char *token;
                        token = strtok(buffer, DELIM);
                        // nu se mai verifica daca comenzile primite sunt valide
                        // intrucat aceasta verificare a fost facuta de clientul
                        // TCP inainte de a trimite comanda
                        if (strcmp(token, "subscribe") == 0) {
                            token = strtok(NULL, DELIM);
                            string topic(token);

                            token = strtok(NULL, DELIM);
                            int SF = atoi(token);

                            // se adauga un abonament nou pentru client
                            client_subscriptions[client_id][topic] = SF;
                        } else if (strcmp(token, "unsubscribe") == 0) {
                            token = strtok(NULL, DELIM);
                            string topic = token;

                            if (client_subscriptions[client_id].find(topic) ==
                                client_subscriptions[client_id].end()) {
                                fprintf(stderr,
                                        "Client is not subscribed to this "
                                        "topic!\n");
                            } else {
                                client_subscriptions[client_id].erase(topic);
                            }
                        }
                    }
                }
            }
        }
    }

    close_all_clients(fdmax, read_fds, tcp_sock, udp_sock);
    close(tcp_sock);
    close(udp_sock);

    return 0;
}
