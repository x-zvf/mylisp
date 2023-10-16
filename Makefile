CC = gcc
CFLAGS = -Wall -g -Werror -Wextra -pedantic -std=c99 -ggdb

OBJS := interpreter.o

all: interpreter

interpreter: $(OBJS)
	$(CC) $(CFLAGS) -o interpreter $(OBJS)

.PHONY: clean all

clean:
	rm -f *.o interpreter
