
# Standard no-strings-attched compilation of the test TCP/IP app and
# its accompanying shared library.  The shared library preloads the
# app and intercepts selected TCP/IP calls to the network stack.
# 


CC = gcc
CFLAGS = -ggdb -Wall -fPIC -DPIC
CFLAGS += -I../../../common
WRAPPER = ../wrapper

TCP_IP_APP_NAME=test_client

all: $(TCP_IP_APP_NAME) PRE_LOAD_LIB 

$(TCP_IP_APP_NAME): $(TCP_IP_APP_NAME).c
	$(CC) $(CFLAGS) $^ -o $@

PRE_LOAD_LIB: 
	$(MAKE) -C $(WRAPPER)

clean:
	rm -f $(TCP_IP_APP_NAME)
	$(MAKE) clean -C $(WRAPPER)
