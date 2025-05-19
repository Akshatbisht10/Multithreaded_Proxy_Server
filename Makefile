CC = gcc
CFLAGS = -g -Wall `pkg-config --cflags gtk+-3.0` -pthread
LDFLAGS = `pkg-config --libs gtk+-3.0` -L/usr/lib/x86_64-linux-gnu -lpthread

all: proxy

proxy: updated_proxy.c
	$(CC) $(CFLAGS) -o proxy.o -c updated_proxy.c
	$(CC) $(CFLAGS) -o proxy proxy.o $(LDFLAGS)

clean:
	rm -f proxy *.o

tar:
	tar -cvzf ass1.tgz updated_proxy.c README Makefile
