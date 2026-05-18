
CC     = gcc
CFLAGS = -Wall -g
OBJS   = csv_parser.o
 
# Ejecutables a generar
TARGETS = p2-server p2-client
 
all: $(TARGETS)
 
p2-server: p2-server.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
 
p2-client: p2-client.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
 
# Regla genérica: compilar cualquier .c que dependa de common.h
%.o: %.c common.h
	$(CC) $(CFLAGS) -c $<
 
clean:
	rm -f *.o $(TARGETS)
 