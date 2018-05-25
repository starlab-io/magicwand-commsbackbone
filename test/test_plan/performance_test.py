import os
import socket
import subprocess

hostname = "wordpress_hostname"
FNULL = open( os.devnull, 'w' );

fout = open( "output.txt", 'w' );


print "\n", hostname, " resolves to " + socket.gethostbyname( hostname ), "\n";

response = subprocess.call( [ "ping", "-c", "1", hostname ], stdout=FNULL );

if response == 0:
    print hostname, 'is up!\n'
else:
    print hostname, 'is down!\n'
    exit(1);

    
#print "Concurrency level       Average time per request (ms)        Failures "
fout.write( "Concurrency level       Average time per request (ms)        Failures\n" )

for i in range( 200, 5001, 200 ) :

    num = i * 5;
    
    process = subprocess.Popen( ["ab", "-n " + str(num), "-c " + str(i), "http://" + hostname + "/" ],
                                stdout=subprocess.PIPE );

    for line in process.stdout:
        if "Failed requests:" in line:
            failed = line.split()[2]
        if "Time per request:" in line:
            time = line.split()[3]
        if "Concurrency Level:" in line:
            concurrency = line.split()[2]
            
    column = concurrency + "\t\t\t\t" + time + "\t\t\t\t" + failed +"\n"; 

    fout.write( column );

    process.kill()

fout.close();
print ""

