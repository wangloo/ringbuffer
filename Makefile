NAME = ringbuf

OBJ_DIR ?= ./build
SRC_DIR ?= .
INC_DIR ?= .
BIN_DIR ?= .

BINARY = $(BIN_DIR)/$(NAME)
SRCS = $(SRC_DIR)/ringbuf.c \
	   $(SRC_DIR)/ringbuf_test.c
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
INCS = $(addprefix -I, $(INC_DIR))

CFLAGS +=  -Wall -g
LDFLAGS += -O2

$(BINARY): $(OBJS)
	@echo +LD $@
	@gcc $(LDFLAGS) -o $@ $^ 
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo +CC $<
	@mkdir -p $(dir $@)
	@gcc $(CFLAGS) $(INCS) -c -o $@ $<

run: $(BINARY)
	@echo [RUN] $^
	@$(BINARY)
clean:
	@echo [CLEAN]
	-rm -rf $(OBJ_DIR) $(BINARY)


