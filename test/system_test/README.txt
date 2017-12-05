PVM_IP, _GW, RUMP_IP, MWROOT, PVM_USER, and XD3_INS_RUN_FILE environment variables are required by magicwand.

Optional env vars for test scripts: PVM_CONF (If not set, defaults to /etc/xen/pvm.cfg), PVM_HOSTNAME (Defaults to pvm)

To run autonomously through Jenkins, the running user (On dom0) will need to be granted passwordless sudo to the following files:
/usr/sbin/xl
/usr/bin/xenstore-write
magicwand-commsbackbone/test/system_test/mw_wait_for_ins.py

The user specified in PVM_USER will need passwordless sudo to the following files:
/home/<USER>/protvm/user/tcp_ip_server/run_tcp_ip_app.sh
/home/<USER>/protvm/user/tcp_ip_server/test_ins_threads_server.sh
/home/<USER>/protvm/user/tcp_ip_server/kill_test_servers.sh
/home/<USER>/protvm/user/http_server/run_tcp_ip_app.sh
/sbin/insmod
/sbin/rmmod
/bin/sh
/bin/cat

PVM will be destroyed at start if PVM_HOSTNAME is set correctly
Existing INS(s) will be destroyed automatically at beginning of test.
INS(s) created for testing will be destroyed at the end of the run.
PVM will be auto-shutdown if PVM_HOSTNAME is set correctly
