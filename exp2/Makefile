all: server client

server: server.c shm.h
	gcc -o server server.c

client: client.c shm.h
	gcc -o client client.c

clean:
	rm -f server client

.PHONY: all clean
