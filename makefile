PORT=58321
CFLAGS = -DSERVER_PORT=$(PORT) -g -Wall -Werror -fsanitize=address

all: battle

battle: battle.o client.o helpers.o
	gcc ${CFLAGS} -o $@ $^

%.o: %.c
	gcc ${CFLAGS} -c $<

clean:
	rm -f *.o battle