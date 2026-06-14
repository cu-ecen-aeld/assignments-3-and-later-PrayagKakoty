# Is compiled by the “all” and “default” target of a Makefile in the “server” directory 
# supports cross compilation, placing the executable file in the “server” directory 
# named aesdsocket.
CC := $(CROSS_COMPILE)gcc
CFLAGS := -g -Wall -Werror

TARGET := aesdsocket
SRC := aesdsocket.c
OBJ := aesdsocket.o

default: all

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
