OBJS=server.o net.o file.o mime.o

server: $(OBJS)
	gcc -Wall -Wextra -g -o $@ $^

net.o: net.c net.h
	gcc -Wall -Wextra -c $<

server.o: server.c net.h
	gcc -Wall -Wextra -c $<

file.o: file.c file.h
	gcc -Wall -Wextra -c $<

mime.o: mime.c mime.h
	gcc -Wall -Wextra -c $<

.PHONY: clean

clean:
	rm -f $(OBJS)
