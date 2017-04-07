#/bin/bash

##
## Plots the data files in the given directory. For quick assessments,
## not for pretty graphs.
##


if [ -z "$1" ]; then
    echo "Usage: $0 <directory>"
    exit 1
fi

out=plot.gnuplot
#data=$(ls $1)

echo > $out

echo -n "plot " > $out

for f in $(ls $1); do
    echo -n "'$1/$f' with lines, " >> $out
done

echo >> $out
echo "pause -1 'Hit ENTER key to continue'" >> $out

gnuplot $out
