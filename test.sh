#!/bin/bash
#
# A very primitive way to test it

make || exit

./timestart 2 60 3 100 bash -c 'n=$[1+RANDOM%100]; echo -n $n; sleep $n; echo -n .' 3>&1 &

par=$!
trap 'kill -1 $par' 0

echo "press return to see statistics"
while	kill -0 $par && read -rt15
do
	kill -WINCH $par
	sleep 1
done
