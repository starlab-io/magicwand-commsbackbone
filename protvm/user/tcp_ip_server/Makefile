#
# Standard no-strings-attched compilation of the test TCP/IP app and
# its accompanying shared library.  The shared library preloads the
# app and intercepts selected TCP/IP calls to the network stack.
# 

CC = gcc
CFLAGS = -Wall -fPIC -DPIC
CFLAGS += -I../../../common

LD = ld
LDFLAGS = -shared

TCP_IP_APP_NAME=test_server
PRE_LOAD_LIB_NAME=wrap_test_server

all: $(TCP_IP_APP_NAME) $(PRE_LOAD_LIB_NAME).o $(PRE_LOAD_LIB_NAME).so

$(TCP_IP_APP_NAME): $(TCP_IP_APP_NAME).c
	@echo ""
	$(CC) $? -o $@ 

$(PRE_LOAD_LIB_NAME).o: $(PRE_LOAD_LIB_NAME).c
	$(CC) $(CFLAGS) -c $? -o $@

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
	rm -f $(TCP_IP_APP_NAME) $(PRE_LOAD_LIB_NAME).o $(PRE_LOAD_LIB_NAME).so 

