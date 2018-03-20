

PVM_IP, _GW, RUMP_IP, MWROOT, PVM_USER, and XD3_INS_RUN_FILE environment variables are required by magicwand.

Optional env vars for test scripts: PVM_CONF (If not set, defaults to /etc/xen/pvm.cfg), PVM_HOSTNAME (Defaults to pvm)

mw_run_full_test.py expects to be run with sudo privileges

The user specified in PVM_USER will need passwordless sudo to the following files on the PVM:
/bin/sh
/bin/cat
/sbin/insmod
/sbin/rmmod
/home/<user>/protvm/user/tcp_ip_server/run_tcp_ip_app.sh
/home/<user>/protvm/user/http_server/run_tcp_ip_app.sh
<The application used for the connection spam test>

For multi-ins testing, the script assumes that mw_distro_ins has port forwarded from localhost to the proper INS.

PVM will be destroyed at start if PVM_HOSTNAME is set correctly
Existing INS(s) will be destroyed automatically at beginning of test.
INS(s) created for testing will be destroyed at the end of the run.
PVM will be auto-shutdown if PVM_HOSTNAME is set correctly
