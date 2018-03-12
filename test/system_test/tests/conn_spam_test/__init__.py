import os
import inspect
import unittest
import subprocess
import signal
import sys
import time
import socket
import threading
import random
import resource

TEST_IP = "127.0.0.1"

def load_tests( loader, tests, pattern ):
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(TestSpam))
    return suite

class RecvThread(threading.Thread):

    def __init__( self, socket_list ):
        super(RecvThread, self).__init__()
        self.socks = socket_list
        self.exit = False

    def run( self ):
        while ( not self.exit ):
            if ( len(self.socks) > 0 ):
                i = random.randint(0, len(self.socks)-1)
                try:
                    while (True):
                        # By default, use port 1024
                        data = self.socks[i].recv(1024)
                except:
                    1+1

                try:
                    self.socks[i].shutdown( socket.SHUT_RDWR )
                    self.socks[i].close()
                    del self.socks[i]
                except:
                    1+1

            time.sleep( random.random() * 0.4 )

class TestSpam(unittest.TestCase):

    # Start server
    def setUp(self):
        self.init_path = os.path.dirname( os.path.abspath(inspect.stack()[0][1]) )

        outfile = open( "{0}/server.log".format(self.init_path), 'w' )
        self.ssh_pid = subprocess.Popen( ['ssh', '{0}@{1}'.format(os.environ['PVM_USER'], os.environ['PVM_IP']), "-t", "-t", "-n", "cd ~/Python/chatserver/server && sudo ./run_tcp_ip_app.sh"], stdout = outfile, stderr = outfile ).pid

        time.sleep(5)
        
    # Start spamming connections
    '''def testClient(self):

        with open( "{0}/client.log".format(self.init_path), 'w' ) as clientout:
            results = subprocess.run( ["{0}/protvm/user/connection_test/connection_test".format(os.environ['MWROOT']), "450", TEST_IP], stdout = clientout, stderr = clientout )

        self.assertEqual( results.returncode, 0, "Client failed with error: {0}".format(results.returncode) )
'''
    def testSpam(self):

        ret = 0
        total_socks = 0
        
        with open( "{0}/client2.log".format( self.init_path ), 'w' ) as clientlog:

            socks = []
            thread = RecvThread(socks)
            thread.start()

            while ( len(socks) < 256 ):

                s = socket.socket()
                try:
                    s.connect((TEST_IP, 1024))
                except Exception as e:
                    ret = e
                    break
                
                s.settimeout(0.05)
                socks.append(s)
                total_socks = total_socks + 1

                time.sleep( random.random() ** 3 * 0.3 )

            clientlog.write( "Total sockets created: {0}\n".format(total_socks) )

            thread.exit = True

            for i in socks:
                i.shutdown( socket.SHUT_RDWR )
                i.close()
                clientlog.write("-")

        self.assertEqual( ret, 0, "Failed after successfully creating {0} sockets.".format(total_socks) )

    # Close server
    def tearDown(self):
        os.kill(self.ssh_pid, signal.SIGTERM)

if __name__ == '__main__':

    if os.geteuid() == 0:                                           
        resource.setrlimit( resource.RLIMIT_NOFILE, (65536, 65536) )


    suite = unittest.TestSuite()
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(TestSpam))

    runner = unittest.TextTestRunner()
    results = runner.run(suite)

    print(str(results))
