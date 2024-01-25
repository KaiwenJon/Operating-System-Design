rmmod mp2
make
insmod mp2.ko

echo "Module insert Done"
sleep 2
./userapp 1000 500 &
sleep 0.7
./userapp 500 10 &
