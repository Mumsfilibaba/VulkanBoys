reset
set terminal wxt size 1000, 600
#set terminal png size 1000, 600
#set output 'challenge3.png'

#params
set multiplot layout 1,2 title "Dexterity and Intelligence among Characters" font ",16"
set title "Dexterity distribution" font ",12"
set xrange [0:3]
set yrange [0:20]
set xtic ("Human" 1, "Firbolg" 2)
set ytic 1
set grid y
set boxwidth 0.7
set xlabel "Character"
set ylabel "Value on dice"

#styles for distribution graph
set style data boxplot
set style fill solid 0.8 noborder

set style line 1 \
    linecolor rgb '#111111'

set style line 2 \
    linecolor rgb '#777777'

set style line 3 \
    linecolor rgb '#111111' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 1.5

set style line 4 \
    linecolor rgb '#777777' \
    linetype 1 linewidth 2 \
    pointtype 5 pointsize 1.5

set style line 5 \
    linecolor rgb '#111111' \
    linetype 1 linewidth 1 \

set style line 6 \
    linecolor rgb '#777777' \
    linetype 1 linewidth 1 \

file = "Orb.tsv"
plot file using (1.0):2 title "Dexterity (Human)" ls 1, file using (2.0):5 title "Dexterity (Firbolg)" ls 2

#time graph
set xrange [1:10]
set yrange [0:25]
set title "Time Trends" font ",12"
set xlabel "Value on dice"
set ylabel "Time (Dice roll iteration)"
set xtic 1
set ytic 1
set grid

f1(x) = a1+b1*x
f2(x) = a2+b2*x

fit [1:30] f1(x) file using 1:3 via a1,b1
fit [1:30] f2(x) file using 1:6 via a2,b2

plot    file using 1:3 with linespoint title "Intelligence (Human)" ls 3, \
        file using 1:6 with linespoint title "Intelligence (Firbolg)" ls 4, \
        f1(x) title "Trendline (Intelligence Human)" ls 5, \
        f2(x) title "Trendline Intelligence (Firbolg)" ls 6, \