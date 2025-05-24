CC = gcc
CFLAGS = -g -Wall -pthread $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS = -pthread $(shell pkg-config --libs gtk+-3.0)
SRC = main.c proxy.c cache.c gui.c
OBJ = $(SRC:.c=.o)
TARGET = proxy

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
