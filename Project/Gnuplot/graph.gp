#parameters
reset
set terminal wxt size 1000, 600

#parameters
set title "Sorrows Per Day" font ",16"
set yrange [0:35]
set auto x
set xlabel "Time"
set ylabel "Sorrows"
set boxwidth 0.75

#style
set style data histogram
set style histogram cluster gap 1
set style fill solid 0.8 noborder
set style line 5 \
    linecolor rgb '#000000' 
set style line 6 \
    linecolor rgb '#555555'
set style line 7 \
    linecolor rgb '#999999'

file = "data.tsv"
plot file using 1:xtic(1) ti col, '' u 2 ti col, '' u 3 ti col, '' u 4 ti col