#parameters
reset
set terminal wxt size 1000, 600

#parameters
set title "Sorrows Per Day" font ",16"
set yrange [0:350]
set xrange [0.25:13.75] 
set xlabel "Time"
set ylabel "Sorrows"
set xtic 1
set ytic 25
set grid y
set boxwidth 0.75

#style
set style data histogram
set style histogram cluster
set style fill solid 0.8 noborder
set style line 5 \
    linecolor rgb '#000000'
set style line 6 \
    linecolor rgb '#555555'
set style line 7 \
    linecolor rgb '#999999'

#Colored rectangle
LABEL = "Label in a box"
set object 1 rect from 7.65,0 to 8.6,100 fc rgbcolor "#88999999" front
set label 4 at 8.15,60 "Safe\nInterval" front center

file = "Sorrows.tsv"
plot file using 3:xticlabels(2) title "Day 1" ls 5, file using 4 title "Day 2" ls 6, file using 5 title "Day 3" ls 7