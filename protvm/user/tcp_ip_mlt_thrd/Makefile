#
# Standard no-strings-attched compilation of the test TCP/IP app and
# its accompanying shared library.  The shared library preloads the
# app and intercepts selected TCP/IP calls to the network stack.
# 

CC = gcc
CFLAGS = -Wall -fPIC -DPIC
CFLAGS += -I..
CFLAGS += -I../../../common

LD = ld
LDFLAGS = -shared
LIBS = -lpthread

TCP_IP_APP_NAME=test_tcpip_app
PRE_LOAD_LIB_NAME=wrap_test_server
LIST_SRCS = list.c sock_info_list.c
LIST_OBJ = list.o sock_info_list.o

all: list $(TCP_IP_APP_NAME) $(PRE_LOAD_LIB_NAME).o $(PRE_LOAD_LIB_NAME).so

list: $(LIST_SRCS)
	$(CC) -c $?

$(TCP_IP_APP_NAME): $(TCP_IP_APP_NAME).c
	@echo ""
	$(CC) $? -o $@ $(LIBS)

$(PRE_LOAD_LIB_NAME).o: $(PRE_LOAD_LIB_NAME).c
	$(CC) $(CFLAGS) -c $? -o $@
	#$(CC) $(CFLAGS) -o $@ $?

$(PRE_LOAD_LIB_NAME).so: $(PRE_LOAD_LIB_NAME).o
	$(LD) $(LDFLAGS) $? -o $@ 
	@echo ""
	@echo "Execute the pre-loaded tcp/ip app by doing:"
	@echo ""
	@echo "./run_tcp_ip_app.sh"
	@echo ""
	@echo "IMPORTANT: First ensure the following are running:"
	@echo "1> char device driver on the PVM"
	@echo "2> Rump unikernel"
	@echo ""

clean:
	rm -f $(TCP_IP_APP_NAME) $(PRE_LOAD_LIB_NAME).o $(LIST_OBJ) $(PRE_LOAD_LIB_NAME).so 

