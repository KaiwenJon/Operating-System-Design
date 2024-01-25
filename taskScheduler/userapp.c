#include "userapp.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>


#define PROCFS_PATH "/proc/mp2/status"

void REGISTER(int pid, unsigned long period, unsigned long processing_time){
    FILE* fp = fopen(PROCFS_PATH, "w");
    if(!fp){
        perror("Failed to open ProcFS for write");
        exit(1);
    }
    fprintf(fp, "R,%d,%lu,%lu", pid, period, processing_time);
    fclose(fp);
}

void YIELD(int pid){
    FILE* fp = fopen(PROCFS_PATH, "w");
    if(!fp){
        perror("Failed to open ProcFS for write");
        exit(1);
    }
    fprintf(fp, "Y,%d", pid);
    fclose(fp);
}

void DEREGISTER(int pid){
    FILE* fp = fopen(PROCFS_PATH, "w");
    if(!fp){
        perror("Failed to open ProcFS for write");
        exit(1);
    }
    fprintf(fp, "D,%d", pid);
    fclose(fp);
}

int check_pid(pid_t pid){
    FILE* fp = fopen(PROCFS_PATH, "r");
    if(!fp){
        perror("Failed to open ProcFS for write");
        return 0;
    }

    char line[256];
    int listed_pid;
    unsigned long period, processing_time;

    int found = 0;
    while(fgets(line, sizeof(line), fp)){
        if(sscanf(line, "%d: %lu, %lu", &listed_pid, &period, &processing_time) == 3){
            // printf("%d: %lu, %lu\n", listed_pid, period, processing_time);
            if(listed_pid == pid){
                found = 1;
            }
        }
    }

    fclose(fp);
    return found;
}

unsigned long factorial(int num){
    if(num == 0 || num == 1) return 1;
    unsigned long result = 1;
    for(int i=1; i<=num; i++){
        result *= i;
    }
    return result;
}

// ./userapp 5000 250 & ./userapp 500 100

int main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(stderr, "Usage: %s <period ms> <processing_time ms>\n", argv[0]);
        return 1;
    }

    pid_t pid = getpid();
    unsigned long period = strtoul(argv[1], NULL, 10);;
    unsigned long processing_time = strtoul(argv[2], NULL, 10);
    struct timespec t0, t1;
    unsigned long wakeup_time, process_time;

    REGISTER(pid, period, processing_time);

    if(!check_pid(pid)){
        fprintf(stderr, "Process was not admitted\n");
        exit(1);
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    YIELD(pid);

    int job_iterations = 3;
    while(job_iterations > 0){
        clock_gettime(CLOCK_MONOTONIC, &t1);
        wakeup_time = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;

        factorial(processing_time * 100000000 / 230); 
        // 100000000 230 ms; 
        //  50000000 122 ms;
        //  25000000  61 ms;

        clock_gettime(CLOCK_MONOTONIC, &t1);
        process_time = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 - wakeup_time;
        printf("Job done: pid: %d, process time: %lu, period: %lu\n", pid, process_time, period);
        // printf("Doing calculation on pid: %d, processing time: %lu\n", pid, process_time);
        job_iterations--;
        YIELD(pid);
    }
    DEREGISTER(pid);
    return 0;
}

