CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -pthread

servidor: server.c
	$(CC) $(CFLAGS) -o servidor server.c $(LDFLAGS)

clean:
	rm -f servidor logs/*.log

.PHONY: clean
