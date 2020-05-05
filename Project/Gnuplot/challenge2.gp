#parameters
reset
set terminal wxt size 1200, 600
#set terminal png size 1000, 600
#set output 'challenge2.png'
set multiplot layout 1,3 title "Character attribute dice rolls" font ",16"

#parameters for clustered bar plot
set style data histogram
set style histogram cluster
set style histogram gap 3
set style fill solid 1 noborder
set style line 5 lc rgb '#000000'
set style line 6 lc rgb '#555555'
set style line 7 lc rgb '#999999'
set style line 8 lc rgb '#000000' lw 1
set style line 9 lc rgb '#555555' lw 1
set style line 10 lc rgb '#999999' lw 1
set boxwidth 1
set xtic 1
set ytic 1
set xrange [1:12] 
set yrange [0:30]
set xlabel "Roll"
set ylabel "Dice Face"

file = "Finas.tsv"

f1(x) = a
f2(x) = b
f3(x) = c

#Half Orc
set title "Half Orc" font ",12"
#calculate avg
fit [1:10] f1(x) file using 1:2 via a
fit [1:10] f2(x) file using 1:3 via b
fit [1:10] f3(x) file using 1:4 via c
plot    file using 2:xticlabels(1) title "Strength (d20)" ls 5, file using 3 title "Dexterity (d12)" ls 6, file using 4 title "Endurance (d10)" ls 7, \
        f1(x) title "Avg. Strength" ls 8, f2(x) title "Avg. Dexterity" ls 9, f3(x) title "Avg. Endurance" ls 10

#Human
set title "Human" font ",12"
#calculate avg
fit [1:10] f1(x) file using 1:6 via a
fit [1:10] f2(x) file using 1:7 via b
fit [1:10] f3(x) file using 1:8 via c
plot    file using 6:xticlabels(5)   title "Strength (d8)" ls 5, file using 7     title "Dexterity (d20)" ls 6, file using 8    title "Endurance (d10)" ls 7, \
        f1(x) title "Avg. Strength" ls 8, f2(x) title "Avg. Dexterity" ls 9, f3(x) title "Avg. Endurance" ls 10

#Firbolg
set title "Firbolg" font ",12"
#calculate avg
fit [1:10] f1(x) file using 1:10 via a
fit [1:10] f2(x) file using 1:11 via b
fit [1:10] f3(x) file using 1:12 via c
plot    file using 10:xticlabels(9)  title "Strength (d6)" ls 5, file using 11    title "Dexterity (d8)" ls 6, file using 12   title "Endurance (d6)" ls 7,  \
        f1(x) title "Avg. Strength" ls 8, f2(x) title "Avg. Dexterity" ls 9, f3(x) title "Avg. Endurance" ls 10