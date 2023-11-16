#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <libgen.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "server.h"

int server_socket;
client_id_t my_id;

void stop() {
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);

    exit(0);
}

void* listener(void* args) {
    char* msg = malloc(MAX_MESSAGE);
    if(msg == NULL) {
        fprintf(stderr, "Błąd alokacji pamięci");
    }

    while(1) {
        int return_value = recv(server_socket, msg, MAX_MESSAGE, 0);
        if(return_value == -1) {
            perror("Błąd odbierania wiadomości");
            stop();
        } else if(return_value == 0) {
            stop();
        } else {
            if((unsigned char) msg[0] == SERVER_KEY) {
                stop();
            } else {
                printf("%s\n", msg+1);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if(!(argc == 4 || argc == 5)) {
        fprintf(stderr, "Błędna liczba parametrów\n");
        return 1;
    }

    char* username = argv[1];
    char* connection_type = argv[2];

    if(strcmp(connection_type, "unix") == 0) {
        char* socket_path = argv[3];

        server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if(server_socket == -1) {
            perror("Błąd tworzenia socketa unix");
            exit(1);
        }

        struct sockaddr_un unix_sockaddr = {0};
        unix_sockaddr.sun_family = AF_UNIX;
        strcpy(unix_sockaddr.sun_path, socket_path);

        if(connect(server_socket, (struct sockaddr*)&unix_sockaddr, sizeof(unix_sockaddr)) == -1) {
            perror("Błąd łączenia z serwerem");
            exit(1);
        }
    } else if(strcmp(connection_type, "tcp") == 0) {
        char* address = argv[3];

        errno = 0;
        char* end;
        in_port_t tcp_port = strtol(argv[4], &end, 0);
        if(*end != '\0' || errno != 0) {
            perror("Błąd odczytania liczby");
            return 1;
        }
        
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if(server_socket == -1) {
            perror("Błąd tworzenia socketa TCP");
            exit(1);
        }
        
        struct sockaddr_in tcp_sockaddr = {0};
        tcp_sockaddr.sin_family = AF_INET;
        tcp_sockaddr.sin_port = htons(tcp_port);
        if(inet_pton(AF_INET, address, &tcp_sockaddr.sin_addr)<=0) {
            printf("Błąd odczytu adresu\n");
            return 1;
        }

        if(connect(server_socket, (struct sockaddr*)&tcp_sockaddr, sizeof(tcp_sockaddr)) == -1) {
            perror("Błąd łączenia z serwerem");
            exit(1);
        }
    } else {
        fprintf(stderr, "Nierozpoznana opcja\n");
        return 1;
    }

    signal(SIGTERM, stop);
    signal(SIGINT, stop);

    char* msg = malloc(MAX_MESSAGE);
    if(msg == NULL) {
        fprintf(stderr, "Błąd alokacji pamięci");
    }
    msg[0] = MESSAGE_INIT;
    strcpy(msg+1, username);
    if(send(server_socket, msg, 1+strlen(username)+1, 0) == -1) {
        perror("Błąd wysyłania wiadomości INIT");
        stop();
    }

    if(recv(server_socket, msg, MAX_MESSAGE, 0) == -1) {
      perror("Błąd odbierania wiadomości INIT");
      stop();
    } else {
        my_id = msg[1];
    }

    pthread_t listener_thread;
    pthread_create(&listener_thread, NULL, listener, NULL);

    char command[5];
    char* message = malloc(MAX_MESSAGE);
    while(1) {
        scanf("%4s", command);

        msg[1] = my_id;

        if(strcmp(command, "LIST") == 0) {
            scanf("%*[^\n]%*c");

            msg[0] = MESSAGE_LIST;
        } else if(strcmp(command, "2ONE") == 0) {
            char* destination = malloc(MAX_USERNAME);
            scanf("%s%*[ ]%[^\n]%*c", destination, message);

            msg[0] = MESSAGE_2ONE;
            snprintf(msg+2, MAX_MESSAGE-3, "%s %s", destination, message);
        } else if(strcmp(command, "2ALL") == 0) { 
            scanf("%*[ ]%[^\n]%*c", message);

            msg[0] = MESSAGE_2ALL;
            strcpy(msg+2, message);
        } else if(strcmp(command, "STOP") == 0) {
            scanf("%*[^\n]%*c");

            msg[0] = MESSAGE_STOP;
        }

        if(send(server_socket, msg, MAX_MESSAGE, 0) == -1) {
            perror("Błąd wysyłania wiadomości");
            stop();
        }

        if(strcmp(command, "STOP") == 0) {
            stop();
        }
    }
}