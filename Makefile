all: server

server: server.c
	gcc -o server server.c

clean:
	rm -f server
