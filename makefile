CC = c99
CFLAGS = -Wall -pedantic
SFLAGS = -lpthread
FILE = 'none'
all:
	make server
	make client

server: 
	$(CC) server.c -o server $(CFLAGS) $(SFLAGS)

client: 
	$(CC) client.c -o client $(CFLAGS)

file: 
	$(CC) $(FILE).c -o $(FILE) $(CFLAGS) $(SFLAGS)