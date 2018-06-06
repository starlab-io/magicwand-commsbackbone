import os
import socket
import subprocess
import resource
import argparse



def do_ab( n, c, hostname ):

    global fout
    
    print "\nRunning: ab -n " + str(n) + " -c " + str(c) + "\n"
    
    process = subprocess.Popen( ["ab", "-n " + str(n), "-c " + str(c), "http://" + hostname + "/" ],
                                stdout=subprocess.PIPE );

    for line in process.stdout:
        if "Failed requests:" in line:
            failed = line.split()[2]
        if "Time per request:" in line and "[ms] (mean)" in line:
            time = line.split()[3]
        if "Concurrency Level:" in line:
            concurrency = line.split()[2]
            
    column = concurrency + "\t\t\t\t" + time + "\t\t\t\t" + failed +"\n"; 

    fout.write( column );

    process.kill()


def main():

    global fout

    parser = argparse.ArgumentParser(description="Run ab benchmarks for plot data")


    parser.add_argument('-o', nargs='?',
                        help='Name for output file')

    parser.add_argument('hostname', nargs='?',
                        help="Hostname to connect to e.g. \"http://<hostname>/\"" )

    args = parser.parse_args()
    
    f_name = args.o
    hostname = args.hostname
    
    print "\n", hostname, " resolves to " + socket.gethostbyname( hostname ), "\n";

    FNULL = open('/dev/null', 'r')
    
    response = subprocess.call( [ "ping", "-c", "1", hostname ], stdout=FNULL );

    if response == 0:
        print hostname, 'is up!\n'
    else:
        print hostname, 'is down!\n'
        exit(1);

    prev_rlimit = resource.getrlimit( resource.RLIMIT_NOFILE );
    print "current rlimit: ", prev_rlimit, " setting to 8000"

    resource.setrlimit( resource.RLIMIT_NOFILE, [ 8000, prev_rlimit[1] ] )

    fout = open( f_name + "txt", 'w' );

    fout.write( "Concurrency level       Average time per request (ms)        Failures\n" )

    for i in range( 1, 50, 10 ):
        num = 1000
        do_ab( num, i, hostname );
    
    for i in range( 100, 2000, 100 ):

        num = 2000;
        do_ab( num, i, hostname );

    fout.close();
    print ""

main()
