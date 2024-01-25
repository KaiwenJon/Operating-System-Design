rmmod mp3
make
insmod mp3.ko
nice ./work 1024 R 50000 & nice ./work 1024 L 10000 &
