CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -g
LDLIBS := -lreadline

SRCS := main.c cli.c redis_client.c response_parser.c command_handler.c strbuf.c
OBJS := $(SRCS:.c=.o)
TARGET := my_redis_cli

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
