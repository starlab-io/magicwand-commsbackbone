reset

#define a min and max function
min(a,b) = (a < b) ? a : b
max(a,b) = (a > b) ? a : b

set terminal png enhanced font "arial,10" fontscale 1.0 size 900, 600 
set output 'raw_tcp_ip_vs_tcp_ip_over_INS.png'
set title "TCP/IP comparison: raw VS. INS" font ",10"
set xlabel 'Concurrency'
set ylabel 'Request Time [ms]'

set key box 

stats 'raw.dat' using 1:2 prefix "RAW"
stats 'rump.dat' using 1:2 prefix "RUMP"

#set label 1 gprintf("MIN = %g msec", RUMP_min_y )  at 50000, 40.0000, 0
#set label 2 gprintf("MAX = %g msec", RUMP_max_y )  at 800000, 140, 0

#set label 3 gprintf("MIN = %g msec", RAW_min_y )  at 50000, 3.0000, 0
#set label 4 gprintf("MAX = %g msec", RAW_max_y )  at 800000, 60, 0

set xrange [min(RUMP_min_x, RAW_min_x):max(RUMP_max_x, RAW_max_x)]
set yrange [0:max(RUMP_max_y,RAW_max_y)]

plot 'raw.dat' using 1:2 with lines lw 3 linecolor rgb "blue" title "TCP/IP",         \
     'raw.dat' using 1:3 with lines lw 3 linecolor rgb "yellow" title "Raw Failures", \
     'rump.dat' using 1:2 with lines lw 3 linecolor rgb "red" title "INS",            \
     'rump.dat' using 1:3 with lines lw 3 linecolor rgb "green" title "INS Failures", 
