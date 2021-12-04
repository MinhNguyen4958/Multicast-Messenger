C = gcc
CFLAGS = -Wall -g
.PHONY: clean all
all: receiver sender server

receiver: receiver.c
	${C} ${CFLAGS} -o receiver receiver.c

sender: sender.c
	${C} ${CFLAGS} -o sender sender.c

server: server.c
	${C} ${CFLAGS} -o server server.c -lpthread

clean:
	rm -f *.o receiver sender server