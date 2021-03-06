#!/usr/bin/env python

import os
import socket
import subprocess
import resource
import argparse
from distutils import spawn


MAX_CONCURRENCY = 500

def do_ab( n, c, hostname, fout ):

    if sanity is True:
        n = c + 2

    call = ["ab", "-n " + str(n), "-c " + str(c), "-r", "-s 1000", "http://" + hostname + "/" ]
    
    print "\nRunning: " + str( call )
    
    process = subprocess.Popen( call,
                                stdout=subprocess.PIPE );

    for line in process.stdout:
        if "Time taken for tests:" in line:
            tot_time = line.split()[4]
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

    fout.write( "%-15s %-13s %-13s %-10s %-10s %-10s %-10s\n" %
                ( concurrency,
                  tot_time,
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
    
    fout.write( "%-15s %-13s %-13s %-10s %-10s %-10s %-10s\n" %
                ("concurrency",
                 "total time",
                 "ms per req",
                 "Failures",
                 "50%",
                 "80%",
                 "longest" ) )

    num = 20000
    do_ab( num, 1, hostname, fout )
    
    for i in range( 10, MAX_CONCURRENCY, 10 ):
        do_ab( num, i, hostname, fout )

    call = [ "gnuplot", "line.gnu" ]

    subprocess.call( call )
    
    os.rename( "./graphs/line.png", "./graphs/" + file_name + ".png" )
    os.rename( "./data/line.dat", "./data/" + file_name + ".dat" )

    fout.close();



def plot_scatter_plot( t, c, hostname ):

    if not os.path.exists( "./data" ):
        os.makedirs( "./data" )

    if not os.path.exists( "./graphs" ):
        os.makedirs( "./graphs" )
        
    call = ["ab", "-t " + str(t), "-s 1000", "-c " + str(c), "-g", "./data/testing.tsv", "http://" + hostname + "/" ]

    print "Running: " + str( call )
    
    process = subprocess.call( call );

    call = ["gnuplot", "scatter.gnu"]

    subprocess.call( call )

    os.rename( "./graphs/timeseries.png", "./graphs/" + file_name + "_" + str(c) + ".png" )
    os.rename( "./data/testing.tsv", "./data/" + file_name + "_" + str(c) + ".tsv" )
    


    
def main():

    global file_name
    global sanity
    global hostname

    if spawn.find_executable("gnuplot") is None:
        print "Gnuplot not found on the system, please install"
        exit()

    if spawn.find_executable("ab") is None:
        print "apache-utils not found on the system, please install"
        exit()
        
    parser = argparse.ArgumentParser(description="Run ab benchmarks for plot data")

    group = parser.add_mutually_exclusive_group( required=True )
    group.add_argument( '--raw' , action='store_true')
    group.add_argument( '--rump', action='store_true' )
    
    parser.add_argument( 'hostname', nargs='?',
                         help="Hostname to connect to e.g. \"http://<hostname>/\"" )

    parser.add_argument( '--sanity', action='store_true', default=False )

    args = parser.parse_args()

    
    if args.rump is True:
        file_name = "rump"
    else:
        file_name = "raw"
        
    hostname = args.hostname
    
    sanity = args.sanity

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


    plot_line_graph();

    for i in [1, 10, 50, 100]:
        t = 30
        plot_scatter_plot( t, i, hostname )

    print ""

main()
