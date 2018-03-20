#!/usr/bin/python3

MW_XENSTORE_ROOT = b"/mw"

import os
import shutil
import subprocess
import inspect
import sys
import unittest
import signal
import time
import resource
#import pyxs

class TestPVMDriver(unittest.TestCase):

    def testDriver(self):
        test_results = subprocess.run( ["ssh", "{0}@{1}".format(os.environ['PVM_USER'], os.environ['PVM_IP']), "sudo rmmod mwcomms"], stdout = subprocess.PIPE, stderr = subprocess.STDOUT )
        self.assertEqual( test_results.returncode, 0, "PVM driver could not be shut down: {0}".format( test_results.stdout.decode() ) )


startdir = "{0}/test/system_test".format(os.environ['MWROOT'])

pvm_hostname = os.environ.get('PVM_HOSTNAME', "pvm")

os.chdir( "{0}/ins/ins-rump".format(os.environ['MWROOT']) )

if ( os.geteuid() == 0 ):
    resource.setrlimit( resource.RLIMIT_NOFILE, (65536, 65536) )

#Todo: Check for better way to update environment
subprocess.call( "source RUMP_ENV.sh; env -0 > .tmp.txt", shell=True, executable="/bin/bash" )
with open( ".tmp.txt", "r" ) as file:
    data = file.read(1000000)
    os.environ.update( line.partition('=')[::2] for line in data.split('\0') )

os.chdir(startdir)
rc = subprocess.call("./mw_test_prep.sh", shell=True)

if ( rc != 0 ):
    sys.exit(rc)

if (not os.path.exists("{0}/logs".format(startdir))):
    os.mkdir("{0}/logs".format(startdir))

print()
print("Starting PVM driver")
pvm_kern_log = open( "{0}/logs/pvm_driver.log".format(startdir), "w" )
pvm_driver_pid = subprocess.Popen(["ssh", "-i", "{0}".format(os.environ['PVM_USER_KEY']),"{0}@{1}".format(os.environ['PVM_USER'], os.environ['PVM_IP']), "cd ~/protvm/kernel/mwcomms && ./load.sh"], stdout=pvm_kern_log, stderr=pvm_kern_log ).pid

xtime.sleep(5)

print()
print("Starting INS")

os.chdir( "{0}/util".format(os.environ['MWROOT']) )

front_log = open( "{0}/logs/frontend.log".format(startdir), "w" )
front_pid = subprocess.Popen( "mw_distro_ins.py", stdout=front_log, stderr=front_log ).pid

#Replacing sleep with proper check whether INS is up
#time.sleep(5)

subprocess.call( ["sudo {0}/mw_wait_for_ins.py".format(startdir)], shell=True )
time.sleep(15)
'''with pyxs.Client() as c:
    m = c.monitor()
    m.watch( MW_XENSTORE_ROOT, b"MW INS watcher" )

    events = m.wait()
    for e in events:
        path = e[0]
        value = None
        if ( c.exists( path ) ):
            value = c[path]

        assert path.startswith( MW_XENSTORE_ROOT ), "Unexpected path {0}".format(path)

        '''

os.chdir(startdir)

print()

unittest_suite = unittest.defaultTestLoader.discover( "tests", pattern="*.py" )

unittest_suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(TestPVMDriver))

print("Running unit tests")
runner = unittest.TextTestRunner()
results = runner.run(unittest_suite)

if (not results.wasSuccessful()):
    with open( "{0}/logs/keydump.txt".format(startdir), 'w' ) as outfile:
        subprocess.call( "{0}/util/mw_dumpkeys".format(os.environ['MWROOT']), stdout=outfile, stderr=outfile )
    rc = 1

time.sleep(5)

'''#Check if PVM driver crashed
print("Testing PVM driver")
test_ret = subprocess.call( ["ssh", "{0}@{1}".format(os.environ['PVM_USER'], os.environ['PVM_IP']), "sudo rmmod mwcomms"] )
if ( test_ret == 0 ):
    print("Success!")
else:
    rc = 1'''

print
input()

#subprocess.call( ["sudo", "xl", "destroy", "mw-ins-rump"] )
try:
    os.kill( pvm_driver_pid, signal.SIGTERM )
except ProcessLookupError:
    print("PVM driver SSH process does not exist.")

try:
    os.kill( front_pid, signal.SIGTERM )
except ProcessLookupError:
    print("Front-end application does not exist.")

subprocess.call( ["sudo", "xl", "shutdown", pvm_hostname] )

sys.exit(rc)
