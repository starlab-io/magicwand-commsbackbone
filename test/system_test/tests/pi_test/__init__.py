import os
import unittest
import subprocess
import sys
import time

def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(TestPi))
    return suite

class TestPi(unittest.TestCase):

    def setUp(self):
        outfile = open("server.log", "w")
        subprocess.Popen(["ssh", "{0}@{1}".format(os.environ['PVM_USER'], os.environ['PVM_IP']), "cd ~/protvm/user/tcp_ip_server && sudo ./run_tcp_ip_app.sh"], stdout=outfile, stderr=outfile)

        time.sleep(5)

    def testClient(self):
        this_dir = os.path.dirname(__file__)

        with open("client.log", "w") as outfile:
            results = subprocess.run(["{0}/pi_test.py".format(this_dir), os.environ['RUMP_IP'], "21845"], stdout=outfile, stderr=outfile)

        self.assertEqual(results.returncode, 0, "Test client failed with error: {0}".format(results.returncode))
