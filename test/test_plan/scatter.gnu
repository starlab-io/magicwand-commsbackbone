set terminal png size 1280,720
#set size 1, 1
set output "graphs/timeseries.png"
set title "Benchmark testing"
set key left top

set datafile separator '\t'

stats 'data/testing.tsv' using 2:5 prefix "STATS"

#set label "TEST" at graph 0 , 0

#set label gprintf("MAX = %1.2f msec", (STATS_max/1000000.0)) at graph 0, 1
#set label gprintf("MIN = %1.2f msec", (STATS_min/1000000.0)) at graph 0.5,0
#set label gprintf("AV = %1.2f msec", (STATS_mean/1000000.0)) at graph 5,300
#set label gprintf("STD DEV = %1.2f msec", (STATS_stddev/1000000.0)) at graph 5,275

set yrange [0:STATS_max_y]
set xrange [STATS_min_x:STATS_max_x]

set grid y

set xdata time

set timefmt "%s"

#set format x "%S"

set xlabel 'seconds'

set ylabel "response time (ms)"





plot "data/testing.tsv" every ::2 using 2:5 title 'response time' with points

exit
