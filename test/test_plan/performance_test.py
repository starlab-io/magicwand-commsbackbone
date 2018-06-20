#!/usr/bin/env python

import os
import socket
import subprocess
import resource
import argparse




def do_ab( n, c, hostname, fout ):

    call = ["ab", "-n " + str(n), "-c " + str(c), "-r", "http://" + hostname + "/" ]
    
    print "\nRunning: " + str( call )
    
    process = subprocess.Popen( call,
                                stdout=subprocess.PIPE );

    for line in process.stdout:
        if "Failed requests:" in line:
            failed = line.split()[2]
        if "Time per request:" in line and "[ms] (mean)" in line:
            time = line.split()[3]
        if "Concurrency Level:" in line:
            concurrency = line.split()[2]
        if "50%" in line:
            percentile_50 = line.split()[1]
        if "80%" in line:
            percentile_80 = line.split()[1]
        if "100%" in line:
            longest_request = line.split()[1]
            
    column = concurrency + "\t\t\t\t" + time + "\t\t\t\t" + failed +"\n"; 

    fout.write( "%-15s %-13s %-10s %-10s %-10s %-10s\n" %
                ( concurrency,
                  time,
                  failed,
                  percentile_50,
                  percentile_80,
                  longest_request ) );
    fout.flush()

    process.kill()

    
def plot_line_graph():

    if not os.path.exists( "./data" ):
        os.makedirs( "./data" )

    if not os.path.exists( "./graphs" ):
        os.makedirs( "./graphs" )

    fout = open( "data/line.dat", 'w' );
    
    fout.write( "%-15s %-13s %-10s %-10s %-10s %-10s\n" %
                ("concurrency",
                 "ms per req",
                 "Failures",
                 "50%",
                 "80%",
                 "longest" ) )

    for i in range( 10, 50, 1 ):
        num = 1000
        do_ab( num, i, hostname, fout )
        
    for i in range( 100, 2000, 50 ):
        num = i*5
        do_ab( num, i, hostname, fout )

    call = [ "gnuplot", "line.gnu" ]

    subprocess.call( call )
    
    os.rename( "./graphs/line.png", "./graphs/" + file_name + ".png" )
    os.rename( "./data/line.dat", "./data/" + file_name + ".dat" )

    fout.close();



def do_ab_gnuplot( t, c, hostname ):

    if not os.path.exists( "./data" ):
        os.makedirs( "./data" )

    if not os.path.exists( "./graphs" ):
        os.makedirs( "./graphs" )
        
    call = ["ab", "-t " + str(t), "-c " + str(c), "-g", "./data/testing.tsv", "http://" + hostname + "/" ]

    
    process = subprocess.call( call );

    call = ["gnuplot", "scatter.gnu"]

    subprocess.call( call )

    os.rename( "./graphs/timeseries.png", "./graphs/" + file_name + "_" + str(c) + ".png" )
    
    
def main():

    global file_name

    parser = argparse.ArgumentParser(description="Run ab benchmarks for plot data")

    group = parser.add_mutually_exclusive_group( required=True )
    group.add_argument( '--raw' , action='store_true')
    group.add_argument( '--rump', action='store_true' )
    
    parser.add_argument( 'hostname', nargs='?',
                         help="Hostname to connect to e.g. \"http://<hostname>/\"" )

    parser.add_argument( '--line', action='store_true', default=False )

    args = parser.parse_args()

    
    if args.rump is True:
        file_name = "rump"
    else:
        file_name = "raw"
        
    global hostname
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
    print "current rlimit: ", prev_rlimit, " setting to " + str( prev_rlimit[1] )

    resource.setrlimit( resource.RLIMIT_NOFILE, [ prev_rlimit[1], prev_rlimit[1] ] )


    if args.line is True:
        plot_line_graph();
    else:
        for i in [1, 10, 50, 100]:
            t = 30
            do_ab_gnuplot( t, i, hostname )

    print ""

main()
