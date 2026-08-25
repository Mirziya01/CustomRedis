CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -pthread -MMD -MP -O2

SRC_DIR = src
BUILD_DIR = build

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET = my_redis_server

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) -pthread

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all

run: all
	./$(TARGET)

.PHONY: all clean rebuild run
