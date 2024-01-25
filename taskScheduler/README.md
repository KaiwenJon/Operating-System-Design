# UIUC CS 423 MP2

Your Name: Li-Kai Chuang

Your NetID: likaikc2


## Description
This kernel module provides real-time task scheduling for registered user-level processes. It allows users to register, yield, and deregister tasks and schedules them based on their specified periods and processing times.

## Features
- **Task Registration:** Users can register their processes with the module, specifying the process ID (PID), period, and processing time. The module performs admission control to ensure that the system remains schedulable.

- **Task Yield:** Registered tasks can voluntarily yield the CPU, allowing other tasks to run.

- **Task Deregistration:** Users can deregister tasks, removing them from the scheduling queue.

- **Real-Time Scheduling:** The module schedules tasks in real-time, ensuring that tasks with shorter periods are given priority. It preempts running tasks when necessary to run higher-priority tasks.



## Implementation
### Dispatching Thread
The dispatching thread is responsible for scheduling tasks registered. It runs continuously and periodically wakes up to make scheduling decisions.

- The dispatching thread selects the next ready task with the shortest period and sets it to a "RUNNING" state.
- If there is a task currently running and a higher-priority task is ready, the dispatching thread preempts the running task.
- The dispatching thread utilizes the Linux kernel's scheduling policies to control task execution, including changing the scheduling policy to SCHED_FIFO for the running task.
- It also cooperates with the timer_callback to handle periodic task wake-ups.

### Timer Callback (timer_callback)
The timer_callback function is called when a registered task's period has elapsed. It is responsible for transitioning the task from a "SLEEPING" state to a "READY" state, indicating that the task is ready to run.

- It is associated with a timer for each registered task, which is initialized when the task is registered.
- When the timer_callback is triggered, it changes the state of the task to "READY" and wakes up the dispatching thread.
- This function ensures that tasks are scheduled according to their specified periods.

### Yield Handler
The yield_handler function is called when a registered task voluntarily yields the CPU.

- It is invoked when a task writes a message to the `/proc/mp2/status` file with the "Y" command, indicating a yield operation.
- The yield_handler function adjusts the next wake-up time for the task, ensuring that the task is rescheduled appropriately for its periodic execution.
- It sets the task's state to "SLEEPING" and wakes up the dispatching thread to make scheduling decisions.
