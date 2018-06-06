reset

#define a min and max function
min(a,b) = (a < b) ? a : b
max(a,b) = (a > b) ? a : b

set terminal png enhanced font "arial,10" fontscale 1.0 size 900, 600 
set output 'raw.png'
set title "TCP/IP comparison: raw VS. INS" font ",10"
set xlabel 'Concurrency'
set ylabel 'Request Time [ms]'

set key box left

stats 'raw.dat' using 1:2 prefix "RAW"

#set label 3 gprintf("MIN = %g msec", RAW_min_y )  at 50000, 3.0000, 0
#set label 4 gprintf("MAX = %g msec", RAW_max_y )  at 800000, 60, 0

set xrange [RAW_min_x:RAW_max_x]
set yrange [0:RAW_max_y]

plot 'raw.dat' using 1:2 with lines lw 3 linecolor rgb "blue" title "ms/req",         \
     'raw.dat' using 1:3 with lines lw 3 linecolor rgb "yellow" title "Raw Failures", \
     'raw.dat' using 1:4 with lines lw 3 linecolor rgb "red" title "50%",             \
     'raw.dat' using 1:5 with lines lw 3 linecolor rgb "green" title "80%",  \
     'raw.dat' using 1:6 with lines lw 3 linecolor rgb "purple" title "longest",

reset


set terminal png enhanced font "arial,10" fontscale 1.0 size 900, 600 
set output 'rump.png'
set title "TCP/IP comparison: raw VS. INS" font ",10"
set xlabel 'Concurrency'
set ylabel 'Request Time [ms]'

set key box left

stats 'rump.dat' using 1:2 prefix "RAW"

#set label 3 gprintf("MIN = %g msec", RAW_min_y )  at 50000, 3.0000, 0
#set label 4 gprintf("MAX = %g msec", RAW_max_y )  at 800000, 60, 0

set xrange [RAW_min_x:RAW_max_x]
set yrange [0:RAW_max_y]

plot 'rump.dat' using 1:2 with lines lw 3 linecolor rgb "blue" title "ms/req",         \
     'rump.dat' using 1:3 with lines lw 3 linecolor rgb "yellow" title "Raw Failures", \
     'rump.dat' using 1:4 with lines lw 3 linecolor rgb "red" title "50%",             \
     'rump.dat' using 1:5 with lines lw 3 linecolor rgb "green" title "80%",  \
     'rump.dat' using 1:6 with lines lw 3 linecolor rgb "purple" title "longest",
