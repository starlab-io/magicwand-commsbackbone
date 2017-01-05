#
# Standard no-strings-attched compilation of the test TCP/IP app and
# its accompanying shared library.  The shared library preloads the
# app and intercepts selected TCP/IP calls to the network stack.
# 

CC = gcc
CFLAGS = -Wall -fPIC -DPIC
CFLAGS += -I../../../common
WRAPPER_DIR=../wrapper
DEBUG = -ggdb
PRE_LOAD_LIB_LOC=../wrapper

TCP_IP_APP_NAME=test_server

all: $(TCP_IP_APP_NAME) 

$(TCP_IP_APP_NAME): $(TCP_IP_APP_NAME).c
	$(CC) $(DEBUG) $(CFLAGS) $? -o $@ 
	$(MAKE) -C $(WRAPPER_DIR)

clean:
	rm -f $(TCP_IP_APP_NAME)

