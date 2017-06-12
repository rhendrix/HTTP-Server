CC = gcc
CFLAGS = -Wall -Wextra -pedantic -g -pthread -o

all: server.c
	$(CC) $(CFLAGS) server server.c
