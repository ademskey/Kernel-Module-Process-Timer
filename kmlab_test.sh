#!/bin/sh

sudo rmmod kmlab
make
sudo insmod kmlab.ko
./userapp 10 &
./userapp 15 &
sleep 6
cat /proc/kmlab/status
