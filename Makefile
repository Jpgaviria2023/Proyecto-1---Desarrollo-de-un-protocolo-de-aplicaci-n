CC = gcc
CFLAGS = -Wall -Wextra -std=c11

servidor: server.c
	$(CC) $(CFLAGS) -o servidor server.c

clean:
	rm -f servidor logs/*.log

.PHONY: clean
