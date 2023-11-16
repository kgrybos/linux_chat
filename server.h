#define SERVER_KEY 255

#define MAX_MESSAGE 255
#define MAX_CLIENTS 20
#define MAX_USERNAME 32

#define MESSAGE_PING 6
#define MESSAGE_STOP 5
#define MESSAGE_LIST 4
#define MESSAGE_2ONE 3
#define MESSAGE_2ALL 2
#define MESSAGE_INIT 1

typedef unsigned char client_id_t;

struct msgbuf {
  long mtype;
  char mtext[MAX_MESSAGE];
};