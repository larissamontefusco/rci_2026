CC = gcc
CFLAGS = -Wall -Wextra -g -DDEBUG
OBJ = OWR.o owr_functions.o
DEPS = owr_headers.h

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

OWR: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.o OWR
