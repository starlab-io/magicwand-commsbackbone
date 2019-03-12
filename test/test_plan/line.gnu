reset

#define a min and max function
min(a,b) = (a < b) ? a : b
max(a,b) = (a > b) ? a : b

set terminal png enhanced font "arial,10" fontscale 1.0 size 900, 600 
set output 'graphs/line.png'
set title "TCP/IP comparison: raw VS. INS" font ",10"
set xlabel 'Concurrency'
set ylabel 'Request Time [ms]'

set key box left

stats 'data/line.dat' using 1:2 prefix "RUMP"

set xrange [0:RUMP_max_x]
set yrange [0:RUMP_max_y]


#set label 3 gprintf("MIN = %g msec", RAW_min_y )  at 50000, 3.0000, 0
#set label 4 gprintf("MAX = %g msec", RAW_max_y )  at 800000, 60, 0


plot 'data/line.dat' using 1:2 with lines lw 3 linecolor rgb "blue" title "ms/req",          \
     'data/line.dat' using 1:4 with lines lw 3 linecolor rgb "red" title "Failures",  \
#     'rump.dat' using 1:4 with lines lw 3 linecolor rgb "red" title "50%",              \
#     'rump.dat' using 1:5 with lines lw 3 linecolor rgb "green" title "80%",            \
#     'rump.dat' using 1:6 with lines lw 3 linecolor rgb "purple" title "longest",
