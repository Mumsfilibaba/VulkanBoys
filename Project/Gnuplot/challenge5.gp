reset
set terminal wxt size 1000, 600
#set terminal png size 1000, 600
#set output 'challenge5.png'

#params
set multiplot layout 1,3 title "Joxdhor" font ",16"
file = 'Joxdhor.tsv'

#styles
set style data histogram
set style fill solid noborder
set grid y

set style line 1 \
    linecolor rgb '#000000'

set style line 2 \
    linecolor rgb '#555555'

set style line 3 \
    linecolor rgb '#999999'

set style line 4 \
    linecolor rgb '#000000' \
    linetype 1 linewidth 1.5

set style line 5 \
    linecolor rgb '#555555' \
    linetype 1 linewidth 1.5

#Evade
set title "Evade" font ",12"
set style histogram clustered gap 2
set boxwidth 1
set xtic ("Human" 3, "Half-Orc" 4, "Firbolg" 5) rotate by -45
set ytic 1
set xrange [2.5:5.5] 
set yrange [0:25]
set xlabel " " offset 0, -1
set ylabel "Dexterity (Dice roll)"

plot    file using 2 ls 1 title "Roll 1", \
        file using 3 ls 2 title "Roll 2", \
        file using 4 ls 3 title "Roll 3", \
        8 title "Minimum" ls 5

#Resist
set title "Resist" font ",12"
set style histogram clustered gap 1 title offset 5,0.5 font "Arial-Bold,11"
set boxwidth 1
set xtic ("Human" 4, "Half-Orc" 5, "Firbolg" 6, "Human" 8, "Half-Orc" 9, "Firbolg" 10) 
set ytic 1
set xrange [3:11] 
set yrange [0:25]
set xlabel " " offset 0, -1
set ylabel "Dice roll"

plot    newhistogram "Endurance" at 1, \
        file using 6 ls 1 title "Roll 1", \
        file using 8 ls 2 title "Roll 2", \
        file using 10 ls 3 title "Roll 3", \
        7 title "Minimum" ls 5, \
        newhistogram "Intelligence" at 5, \
        file using 7 ls 1 notitle, \
        file using 9 ls 2 notitle, \
        file using 11 ls 3 notitle \

#Attack
set title "Attack" font ",12"
set style histogram clustered gap 2
set boxwidth 1
set xtic ("Human" 3, "Half-Orc" 4, "Firbolg" 5)
set ytic 1
set xrange [2.5:5.5] 
set yrange [0:25]
set xlabel " " offset 0, -1
set ylabel "Dexterity (Dice roll)"

plot    file using 13 ls 1 title "Roll 1", \
        file using 14 ls 2 title "Roll 2", \
        file using 15 ls 3 title "Roll 3", \
        15 title "Minimum" ls 5, \