#!/usr/bin/bash

make 1 2
export TIMEFORMAT='%2R'
DESCRIPTIONS=(
    ""
    "Mine :)"
    "His :)"
)

for PROGRAM in {1..2}; do
    # run each program 5 times, capturing time output
    TIMES=""
    for n in {1..5}; do
        TIMES+="$({ time ./$PROGRAM ~/dev/billion-rows/measurements1b.txt > /dev/null; } 2>&1 ) "
        sleep 1
    done

    echo -e $TIMES | awk "BEGIN { RS = \" \"; sum = 0.0; } {  sum += \$1; } END { sum /= 5.0; printf \"$PROGRAM.c runtime=[$TIMES] average=%.2fs\t${DESCRIPTIONS[$PROGRAM]}\n\", sum }"
done
