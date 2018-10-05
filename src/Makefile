OBJS=server.o net.o file.o mime.o cache.o hashtable.o llist.o

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

cache.o: cache.c cache.h
	gcc -Wall -Wextra -c $<

hashtable.o: hashtable.c hashtable.h
	gcc -Wall -Wextra -c $<

llist.o: llist.c llist.h
	gcc -Wall -Wextra -c $<

.PHONY: clean

clean:
	rm -f $(OBJS)
	rm -rf build $(TESTS)
	rm -f cache_tests/tests.log

TEST_SRC=$(wildcard cache_tests/*_tests.c)
TESTS=$(patsubst %.c,%,$(TEST_SRC))

TARGET=build/server.a
SO_TARGET=$(patsubst %.a,%.so,$(TARGET))

test:
	tests

tests:
	clean $(TARGET) $(SO_TARGET) tests2

dev: CFLAGS=-g -std=c99 -Wall -Isrc -Wall -Wextra %(OPTFLAGS)
dev: all

build:
	@mkdir -p build
	@mkdir -p bin

.PHONY: tests

tests2: $(TESTS)
	sh ./cache_tests/runtests.sh

