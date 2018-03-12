import os
import shutil
import unittest
import subprocess
import inspect
import sys
import signal
import time

def load_tests( loader, tests, pattern ):
    suite = unittest.TestSuite()
    suite.addTests( loader.loadTestsFromTestCase(TestAB) )
    return suite

class TestAB(unittest.TestCase):

    def setUp(self):
        self.init_path = os.path.dirname( os.path.abspath(inspect.stack()[0][1]) )

        outfile = open( "{0}/outfile.txt".format(self.init_path), 'w' )
        self.ssh_pid = subprocess.Popen( ['ssh', '{0}@{1}'.format(os.environ['PVM_USER'], os.environ['PVM_IP']), "-t", "-t", "-n", "cd ~/protvm/user/http_server && cp index.html /tmp && sudo ./run_tcp_ip_app.sh"], stdout = outfile, stderr = outfile ).pid

        time.sleep(5)

    def testBench(self):
        os.chdir("{0}/test/apache_bench".format(os.environ["MWROOT"]))
        
        results = subprocess.run(["RUMP_IP=localhost ./run_benchmark.sh rump"], stdout = subprocess.DEVNULL, stderr = subprocess.DEVNULL, shell=True )

        self.assertEqual(results.returncode, 0, "Benchmark failed with error: {0}".format(results.returncode))

        try:
            shutil.rmtree("{0}/test_results".format(self.init_path))
        except:
            0-0
        shutil.copytree("results", "{0}/test_results".format(self.init_path))

    def tearDown(self):
        os.kill(self.ssh_pid, signal.SIGTERM)
