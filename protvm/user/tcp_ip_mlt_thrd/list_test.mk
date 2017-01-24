#
# Standard no-strings-attched compilation of the test TCP/IP app and
# its accompanying shared library.  The shared library preloads the
# app and intercepts selected TCP/IP calls to the network stack.
# 

#gcc -I..  -o test_sock_info_list main.c sock_info_list_tests.c
#list.o sock_info_list.o  -pthread -lcheck_pic -lrt -lm

CC = gcc
CFLAGS = -Wall 
#CFLAGS += -I..

LDFLAGS = -pthread -lcheck_pic -lrt -lm

TEST_APP_NAME=test_sock_info_list
LIST_SRCS = main.c sock_info_list_tests.c 
LIST_OBJ = list.o sock_info_list.o
OBJ_DIR = ../wrapper

all: $(TEST_APP_NAME) 

$(TEST_APP_NAME): $(LIST_SRCS) $(LIST_OBJ)
	@echo ""
	$(CC) $(OBJ_DIR)$^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TEST_APP_NAME) 

