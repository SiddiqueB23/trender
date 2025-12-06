SRC_DIR := src
BUILD_DIR := build

CC := gcc
CFLAGS = -Wall -Wextra -pedantic -O3
LDFLAGS = -lm -lc -lpthread -lrt
HEADERS := $(wildcard $(SRC_DIR)/*.h)

hello: $(BUILD_DIR) $(SRC_DIR)/hello.c $(HEADERS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/hello $(SRC_DIR)/hello.c $(LDFLAGS)

tio_input_test: $(BUILD_DIR) $(SRC_DIR)/tio_input_test.c $(HEADERS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/tio_input_test $(SRC_DIR)/tio_input_test.c $(LDFLAGS)

main: $(BUILD_DIR) $(SRC_DIR)/main.c $(HEADERS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/main $(SRC_DIR)/main.c $(LDFLAGS)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	@rm -rf $(BUILD_DIR)