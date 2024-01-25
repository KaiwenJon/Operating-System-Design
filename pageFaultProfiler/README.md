# MP3-PageFaultProfiler
This is a Linux kernel module responsible for profiling and monitoring user-level processes. It collects various performance statistics, such as CPU utilization, minor and mjor page faults, and store this data in a buffer that can be accessed by user-space app through device driver.

## Features
1. Process Monitoring: The module can monitor multiple user-level processes concurrently. It records information about each monitored process, including PID, user time, system time, minor and major pagefaults.

2. Profiling: The module periodically profiles the monitored processes and records the performance statistics in a circular buffer.

3. User Interface: User can perform Register, Unregister, or Read of registered PID through /proc file system.

4. Memory Mapping: It supports memory mapping of the profiler buffer, allowing user-space programs to access and analyze the collected data.

## Implementation

- A mp3_task_struct represents a monitored process and stores its PID, user time, system time, minor and major page faults.

- The work_function is a kernel work function scheduled to run periodically using a delayed work queue. It collects perofrmance statistics for all monitored processes and stores them in the profiler buffer.

- /proc/mp2/status allows user-space programs to register and unregister processes.

- mp3_chrdev_mmap allows user-space programs to memory-map the profiler buffer, providing efficient access to the collected data.

## Results
### Case Study 1: Thrashing and Locality.
In this case we will try to understand the page fault rate as a function of used memory size and memory locality.

- Work process 1: 1024MB Memory, Random Access, and 50,000 accesses per iteration
- Work process 2: 1024MB Memory, Random Access, and 10,000 accesses per iteration

Plot a graph named case_1_work_1_2.png where x-axis is the time and y-axis is the accumulated page fault count of the two work processes (work processes 1 and 2). 
![case_1_work_1_2](https://github.com/cs423-uiuc/mp3-KaiwenJon/assets/70893513/0b1f784d-3898-44ec-89c2-b6b457945148)


Also, conduct another experiment by using:

- Work process 3: 1024MB Memory, Random Locality Access, and 50,000 accesses per iteration
- Work process 4: 1024MB Memory, Locality-based Access, and 10,000 accesses per iteration

Plot another graph named case_1_work_3_4.png where x-axis is the time and y-axis is the accumulated page fault count of the two work processes (work processes 3 and 4).
![case_1_work_3_4](https://github.com/cs423-uiuc/mp3-KaiwenJon/assets/70893513/05147054-86dd-4eb3-960b-a9e09c043825)


We can see a higher rate of page faults in the first graph, which is expected because both the work process use random access pattern, whereas the second experiment uses locality-based access, which could trigger more minor-page faults since we keep requesting space that's new and hasn't been marked as available. Also, the completion of time and major page faults are almost the same for both experiments, since major page faults requires transferring data from disk, which could cost a lot of time. We're not requesting too much space so there's no major page faults triggered.

### Case Study 2: Multiprogramming
In this case we will analyze the CPU utilization as a function of the degree of programming. We will use N instances of the following work:

- Work process 5: 200MB Memory, Random Locality Access, and 10,000 accesses per iteration

Plot a graph named case_2.png where x-axis is N (i.e., 5, 11, 16, 20, 22) and y-axis is the total utilization of all N copies of the work process 5. 
![case_2](https://github.com/cs423-uiuc/mp3-KaiwenJon/assets/70893513/86e1a2ca-6786-4d52-b46b-65fc6c781fdc)

Also a graph where y-axis is the completion of time.
![completion_time_line_plot](https://github.com/cs423-uiuc/mp3-KaiwenJon/assets/70893513/ab699fe6-44b5-43a6-a31c-11fe5295b705)

From the first graph we can observe that when N <= 16, the CPU utilization grows almost linearly. However, when N equals 20 and 22, we start to observe the thrashing behavior, which means CPU is busy with handling paging and page faults, in addition to normal work that it's supposed to do. Also, from the second graph we can see that thrashing causes the processing time longer, slowing down the application when N gets too large.
