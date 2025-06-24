# Makefile for proxy server (Windows/Linux compatible)
CC      := gcc
CFLAGS  := -g -Wall
LDFLAGS := -lpthread

# Windows-specific settings
ifeq ($(OS),Windows_NT)
    LDFLAGS += -lws2_32
    TARGET  := proxy.exe
else
    TARGET  := proxy
endif

SRCS    := proxyServerWithCache.c proxy_parse.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)