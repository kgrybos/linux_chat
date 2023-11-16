#define _GNU_SOURCE

#include <asm-generic/errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <pthread.h>

#include "server.h"

#define MAX_FDS MAX_CLIENTS+2

struct {
  int fd;
  char username[MAX_USERNAME];
} clients[MAX_CLIENTS];
int unix_socket;
int tcp_socket;
int epollfd;
struct epoll_event* events;
FILE* logfile;

int add_client(client_id_t* client_id, int queue, char* username) {
  for(int i = 0; i < MAX_CLIENTS; i++) {
    if(clients[i].fd == -1) {
      clients[i].fd = queue;
      strcpy(clients[i].username, username);
      *client_id = i;
      return 0;
    }
  }

  return -1;
}

void remove_client(int fd) {
  for(int i = 0; i < MAX_CLIENTS; i++) {
    if(clients[i].fd == fd) {
      clients[i].fd = -1;
      break;
    }
  }
}

int list_clients(client_id_t* client_list) {
  unsigned char count = 0;
  for(int i = 0; i < MAX_CLIENTS; i++) {
    if(clients[i].fd != -1) {
      client_list[count] = i;
      count++;
    }
  }
  return count;
}

void print_clients(client_id_t* client_list, int client_count, char* buffer) {
  int index = snprintf(buffer, MAX_MESSAGE, "%d aktywnych klientów:\n", client_count);
  for(int i = 0; i < client_count; i++) {
    index += sprintf(buffer+index, "%s\n", clients[client_list[i]].username);
  }
}

int find_client(char* username) {
  for(int i = 0; i < MAX_CLIENTS; i++) {
    if(clients[i].fd != -1 && strcmp(clients[i].username, username) == 0) {
      return i;
    }
  }
  return -1;
}

void handle_stop(char* msg, int client_fd) {
  client_id_t client_id = msg[1];
  clients[client_id].fd = -1;
}

void handle_list(char* msg, int client_fd) {
  client_id_t client_id = msg[1];

  char* response = malloc(MAX_MESSAGE);
  if(response == NULL) {
    perror("Błąd alokacji pamięci");
    return;
  }
  response[0] = client_id+1;
  client_id_t client_list[MAX_CLIENTS];
  int client_count = list_clients(client_list);

  print_clients(client_list, client_count, response+1);
  if(send(client_fd, response, 1+strlen(response+1)+1, 0) == -1) {
    perror("Błąd odpowiadania na polecenie LIST");
  }

  free(response);
}

void handle_init(char* msg, int client_fd) {
  char* username = msg+1;
  client_id_t client_id;
  if(add_client(&client_id, client_fd, username) == -1) {
    fprintf(stderr, "Za dużo aktywnych połączeń\n");
  } else {
    char response[2];
    response[0] = client_id;
    response[1] = client_id;
    if(send(client_fd, response, 2, 0) == -1) {
      perror("Błąd odpowiadania na polecenie INIT");
    }
  }
}

void handle_2one(char* msg, int client_fd) {
  client_id_t client_id = msg[1];
  char* destination_username = malloc(MAX_USERNAME);
  char* message = malloc(MAX_MESSAGE);
  sscanf(msg+2, "%s%*[ ]%[^\n]%*c", destination_username, message);

  char* response = malloc(MAX_MESSAGE);
  if(response == NULL) {
    perror("Błąd alokacji pamięci");
    return;
  }
  response[0] = client_id;
  strcpy(response+1, message);

  int destination_id = find_client(destination_username);
  if(destination_id == -1) {
    fprintf(stderr, "Błędny użytkownik\n");
  }

  if(send(clients[destination_id].fd, response, 1+strlen(response+1)+1, 0) == -1) {
    perror("Błąd wysyłania wiadomości");
  }
}

void handle_2all(char* msg, int client_fd) {
  client_id_t client_id = msg[1];
  char* message = msg+2;

  client_id_t client_list[MAX_CLIENTS];
  int clients_count = list_clients(client_list);

  char* response = malloc(MAX_MESSAGE);
  if(response == NULL) {
    perror("Błąd alokacji pamięci");
    return;
  }
  response[0] = client_id;
  strcpy(response+1, message);

  for(int i = 0; i < clients_count; i++) {
    if(send(clients[client_list[i]].fd, response, 1+strlen(response+1)+1, 0) == -1) {
      perror("Błąd wysyłania wiadomości");
    }
  }

  free(response);
}

void interrupt(int sig) {
  client_id_t client_list[MAX_CLIENTS];
  int clients_count = list_clients(client_list);

  for(int i = 0; i < clients_count; i++) {
    shutdown(clients[client_list[i]].fd, SHUT_RDWR);
    close(clients[client_list[i]].fd);
  }

  shutdown(tcp_socket, SHUT_RDWR);
  close(tcp_socket);
  shutdown(unix_socket, SHUT_RDWR);
  close(unix_socket);

  fclose(logfile);

  exit(EXIT_SUCCESS);
}

char* get_message_name(long number) {
  if(number == MESSAGE_STOP) {
    return "STOP";
  } else if(number == MESSAGE_LIST) {
    return "LIST";
  } else if(number == MESSAGE_2ONE) {
    return "2ONE";
  } else if(number == MESSAGE_2ALL) {
    return "2ALL";
  } else if(number == MESSAGE_INIT) {
    return "INIT";
  }

  return "INVALID";
}

void init_sockets(in_port_t tcp_port, char* unix_socket_path) {
  tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
  if(tcp_socket == -1) {
    perror("Błąd tworzenia socketa TCP");
    exit(1);
  }

  int true = 1;
  setsockopt(tcp_socket,SOL_SOCKET,SO_REUSEADDR,&true,sizeof(int));
  
  struct sockaddr_in tcp_sockaddr = {0};
  tcp_sockaddr.sin_family = AF_INET;
  tcp_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  tcp_sockaddr.sin_port = htons(tcp_port);

  if(bind(tcp_socket, (struct sockaddr*)&tcp_sockaddr, sizeof(tcp_sockaddr)) == -1) {
    perror("Błąd otwierania portu w sockecie TCP");
    exit(1);
  }

  if(listen(tcp_socket, MAX_CLIENTS) == -1) {
    perror("Błąd listen");
    exit(1);
  }

  unix_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if(unix_socket == -1) {
    perror("Błąd tworzenia socketa unix");
    exit(1);
  }

  setsockopt(unix_socket,SOL_SOCKET,SO_REUSEADDR,&true,sizeof(int));

  struct sockaddr_un unix_sockaddr = {0};
  unix_sockaddr.sun_family = AF_UNIX;
  strncpy(unix_sockaddr.sun_path, unix_socket_path, sizeof(unix_sockaddr.sun_path) - 1);

  unlink(unix_socket_path);
  if(bind(unix_socket, (struct sockaddr*)&unix_sockaddr, sizeof(unix_sockaddr)) == -1) {
    perror("Błąd otwierania portu w sockecie Unix");
    exit(1);
  }

  if(listen(unix_socket, MAX_CLIENTS) == -1) {
    perror("Błąd listen");
    exit(1);
  }

  epollfd = epoll_create1(0);
  if (epollfd < 0) {
    perror("Błąd tworzenia epoll");
  }

  struct epoll_event tcp_event = { .data.fd = tcp_socket, .events = EPOLLIN };
  if(epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_socket, &tcp_event) == -1) {
    perror("Błąd rejestracji deskryptora w epoll");
    exit(1);
  }

  struct epoll_event unix_event = { .data.fd = unix_socket, .events = EPOLLIN };
  if(epoll_ctl(epollfd, EPOLL_CTL_ADD, unix_socket, &unix_event) == -1) {
    perror("Błąd rejestracji deskryptora w epoll");
    exit(1);
  }

  events = calloc(MAX_FDS, sizeof(struct epoll_event));
  if(events == NULL) {
    perror("Błąd alokacji pamięci");
    exit(1);
  }
}

void* handle_sockets(void* args) {
  char* msg = malloc(MAX_MESSAGE);
  if(msg == NULL) {
    perror("Błąd alokacji pamięci");
  }

  while (1) {
    int number_ready = epoll_wait(epollfd, events, MAX_FDS, -1);
    for (int i = 0; i < number_ready; i++) {
      if(events[i].events & EPOLLERR) {
        perror("Błąd odbierania eventu");
        exit(1);
      }

      if(events[i].data.fd == tcp_socket || events[i].data.fd == unix_socket) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(events[i].data.fd, (struct sockaddr*)&client_addr,
        &client_addr_len);
        if(client_socket == -1) {
          perror("Bład akceptacji klienta TCP");
          exit(1);
        }

        struct epoll_event client_event = { .data.fd = client_socket, .events = EPOLLIN };
        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, client_socket, &client_event) == -1) {
          perror("Błąd rejestracji deskryptora w epoll");
          exit(1);
        }
      } else {
        int bytes = recv(events[i].data.fd, msg, MAX_MESSAGE, 0);
        if(bytes == 0) {
          remove_client(events[i].data.fd);

          if(epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL) == -1) {
            perror("Błąd rejestracji deskryptora w epoll");
            exit(1);
          }
        }

        if(bytes < 0) {
          perror("Błąd recv");
          exit(1);
        }

        long msg_type = msg[0];

        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        fprintf(logfile, "%d-%02d-%02d %02d:%02d:%02d - %s %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, get_message_name(msg_type), msg+3);

        if(msg_type == MESSAGE_STOP) {
          handle_stop(msg, events[i].data.fd);
        } else if(msg_type == MESSAGE_LIST) {
          handle_list(msg, events[i].data.fd);
        } else if(msg_type == MESSAGE_INIT) {
          handle_init(msg, events[i].data.fd);
        } else if(msg_type == MESSAGE_2ONE) {
          handle_2one(msg, events[i].data.fd);
        } else if(msg_type == MESSAGE_2ALL) {
          handle_2all(msg, events[i].data.fd);
        }
      }
    }
  }
}

int main(int argc, char* argv[]) {
  if(argc != 3) {
    fprintf(stderr, "Zbyt mała liczba argumentów\n");
    return 1;
  }

  errno = 0;
  char* end;
  in_port_t tcp_port = strtol(argv[1], &end, 0);
  if(*end != '\0' || errno != 0) {
      perror("Błąd odczytania liczby");
      return 1;
  }

  for(int i = 0; i < MAX_CLIENTS; i++) {
    clients[i].fd = -1;
  }

  logfile = fopen("server.log", "w");

  char* unix_socket_path = argv[2];

  init_sockets(tcp_port, unix_socket_path);

  pthread_t socket_handler;
  pthread_create(&socket_handler, NULL, handle_sockets, NULL);

  pthread_join(socket_handler, NULL);

  signal(SIGINT, interrupt);
  signal(SIGTERM, interrupt);
}