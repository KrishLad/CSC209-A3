PORT=54640
CFLAGS= -DPORT=$(PORT) -g -Wall

all: battle  

battle: battle.o
	gcc ${CFLAGS} -o $@ $^

%.o: %.c 
	${CC} ${CFLAGS}  -c $<

clean:
	rm *.o battleserver 