reset
set terminal wxt size 1000, 600
#set terminal png size 1000, 600
#set output 'challenge4.png'

#params
set title "Pallace" font ",16"
set xrange[-10:10]
set yrange[-10:10]
set xlabel "x"
set ylabel "y"
set zlabel "z"

#style
set isosamples 70
set palette defined (-2.5 'black', 0 "white", 2 'blue')

splot   -atan(x*y)*atan(x*y) with pm3d title "Ground: -2.5 < z < 0", \
        atan(x*y)*asin(x*y)+0.5 with pm3d title "Palace: z > 0"