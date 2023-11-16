all: server client

server: server.c server.h
	gcc -g -Wall server.c -o server

client: client.c server.h
	gcc -g -Wall client.c -o client

clean:
	rm server client
