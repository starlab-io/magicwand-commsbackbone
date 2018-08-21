reset

#define a min and max function
min(a,b) = (a < b) ? a : b
max(a,b) = (a > b) ? a : b

set terminal png enhanced font "arial,10" fontscale 1.0 size 900, 600 
set output 'graphs/raw_vs_rump.png'
set title "TCP/IP comparison: raw VS. INS" font ",10"
set xlabel 'Concurrency'
set ylabel 'Request Time [ms]'

set key box left

stats 'data/rump.dat' using 1:2 prefix "RUMP"
stats 'data/raw.dat' using 1:2 prefix "RAW"

set xrange [0:RUMP_max_x]
set yrange [0:RUMP_max_y]


set label 3 sprintf("Raw: \ny = %g x + %g", RAW_slope , RAW_intercept)  at 10.0, 1400.0, 0
set label 4 sprintf("Rump: \ny = %g x + %g", RUMP_slope , RUMP_intercept)  at 10.0, 1200.0, 0
#set label 3 gprintf("MIN = %g msec", RAW_min_y )  at 50000, 3.0000, 0
#set label 4 gprintf("MAX = %g msec", RAW_max_y )  at 800000, 60, 0


plot 'data/raw.dat' using 1:2 with lines lw 3 linecolor rgb "blue" title "raw ms/req",          \
     'data/rump.dat' using 1:2 with lines lw 3 linecolor rgb "red" title "rump ms/req",  \
#     'rump.dat' using 1:4 with lines lw 3 linecolor rgb "red" title "50%",              \
#     'rump.dat' using 1:5 with lines lw 3 linecolor rgb "green" title "80%",            \
#     'rump.dat' using 1:6 with lines lw 3 linecolor rgb "purple" title "longest",
