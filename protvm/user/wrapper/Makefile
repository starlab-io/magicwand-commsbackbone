# Standard no-strings-attched compilation of the test TCP/IP app and
# its accompanying shared library.  The shared library preloads the
# app and intercepts selected TCP/IP calls to the network stack.
# 

CC = gcc
CFLAGS = -Wall -fPIC -DPIC -std=gnu99
CFLAGS += -I.. -I../../../common
CFLAGS += -ggdb
#CFLAGS += -DMYDEBUG
CFLAGS += -DMYTRAP

LD = ld
LDFLAGS = -ggdb -shared
LIBS = -lpthread

PRE_LOAD_LIB_NAME=tcp_ip_wrapper
LIST_SRCS = list.c sock_info_list.c
LIST_OBJ = list.o sock_info_list.o

all: list $(PRE_LOAD_LIB_NAME).o $(PRE_LOAD_LIB_NAME).so

list: $(LIST_SRCS)
	$(CC) -fPIC -c $^

$(PRE_LOAD_LIB_NAME).o: $(PRE_LOAD_LIB_NAME).c
	$(CC) $(CFLAGS) -c $^ -o $@

$(PRE_LOAD_LIB_NAME).so: $(PRE_LOAD_LIB_NAME).o $(LIST_OBJ)
	$(LD) $(LDFLAGS) $^ -o $@ -ldl

clean:
	rm -f $(TCP_IP_APP_NAME) $(PRE_LOAD_LIB_NAME).o $(LIST_OBJ) $(PRE_LOAD_LIB_NAME).so
