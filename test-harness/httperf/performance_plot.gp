set terminal png size 1200,800 enhanced font "Helvetica,20"
set output "/var/log/httperf/performance.png"
set xdata time
set xlabel "Seconds (since start of test)"
set timefmt "%M:%S"
set format x "%M:%S"
set datafile separator ","
set yrange [0:150]
set ylabel "Request Throughput"
set nokey
plot "/var/log/httperf/performance.csv" using 1:2 with lines