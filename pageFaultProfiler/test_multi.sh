rmmod mp3
make
insmod mp3.ko
num_executions=22
for ((i = 1; i <= num_executions; i++)); do
    nice ./work 200 R 10000 &
done
