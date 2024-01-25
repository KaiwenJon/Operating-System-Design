#define LINUX
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include "mp2_given.h"

// !!!!!!!!!!!!! IMPORTANT !!!!!!!!!!!!!
// Please put your name and email here
MODULE_AUTHOR("Li-Kai Chuang likaikc2@illinois.edu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1

#define PROC_DIR "mp2"
#define STATUS_FILE "status"

/* Declarations for proc filesystem directories and files */
static struct proc_dir_entry* proc_mp2_dir;
static struct proc_dir_entry* proc_mp2_status;

typedef enum {
	READY,
	RUNNING,
	SLEEPING
} task_state;

typedef struct {
	pid_t pid;
	unsigned long period; // ms
	unsigned long processing_time; // ms
	unsigned long next_wakeup; // jiffies
	task_state state;
	struct list_head task_node;
	struct timer_list wakeup_timer;
	struct task_struct* linux_task;
} mp2_task_struct;

LIST_HEAD(task_list);

static DEFINE_MUTEX(mutex_list);
static DEFINE_MUTEX(mutex_cur_task);

static struct kmem_cache* mp2_task_cache;
static struct task_struct* dispatching_thread;
static mp2_task_struct* current_running_task = NULL;

/* Kthread responsible for scheduling tasks. Will be waked up on timer_callback and yield_handler */
int dispatching_function(void* data){
	mp2_task_struct* task;
	mp2_task_struct* chosenTask = NULL;
	struct sched_attr attr;
	memset(&attr, 0, sizeof(attr));
	while(!kthread_should_stop()){
		chosenTask = NULL;
		// Find the READY task with shortest period
    	mutex_lock(&mutex_list);
		list_for_each_entry(task, &task_list, task_node){
			if(task->state == READY && (chosenTask == NULL || task->period < chosenTask->period)){
				chosenTask = task;
			}
		}
   		mutex_unlock(&mutex_list);


    	mutex_lock(&mutex_cur_task);

		pr_info("Dispatching Thread waked up!\n");
		if(chosenTask == NULL){
			pr_info("no Ready task\n");
		}
		else{
			pr_info("chosenTask! %d, %lu\n", chosenTask->pid, chosenTask->period);
		}

		if(current_running_task == NULL){
			pr_info("no curernt running task\n");
		}
		else{
			pr_info("current running! %d, %lu, sleeping? %d\n", current_running_task->pid, current_running_task->period, current_running_task->state == SLEEPING);
		}

		// If we have a chosen task, and it has higher prior than current running task (or if curr_run_task is sleeping (it means it's done), we can preempt it!)
		if(chosenTask != NULL && (current_running_task == NULL || chosenTask->period < current_running_task->period || current_running_task->state == SLEEPING)){
			// Preempt the current running task, if there's one
			if(current_running_task != NULL && current_running_task->state == RUNNING){
				pr_info("Preempt the current running task\n");
				attr.sched_policy = SCHED_NORMAL;
				attr.sched_priority = 0;
				sched_setattr_nocheck(current_running_task->linux_task, &attr);
				current_running_task->state = READY;
			}

			pr_info("set the chosen task to running %d\n", chosenTask->pid);
			// Set the chosen task to RUNNING
			current_running_task = chosenTask;
			current_running_task->state = RUNNING;
			wake_up_process(current_running_task->linux_task);
			attr.sched_policy = SCHED_FIFO;
            attr.sched_priority = 99;
            sched_setattr_nocheck(current_running_task->linux_task, &attr);
		}
		else if(chosenTask == NULL && current_running_task != NULL){
			// if no chosen task (no task in READY), simply preempt the current task.
			pr_info("No task is READY, preempt the current running task %d (yield called itself.)\n", current_running_task->pid);
			attr.sched_policy = SCHED_NORMAL;
			attr.sched_priority = 0;
			sched_setattr_nocheck(current_running_task->linux_task, &attr);
			if(current_running_task->state == RUNNING){
				current_running_task->state = READY;
			}
			current_running_task = NULL;
		}
    	mutex_unlock(&mutex_cur_task);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return 0;
}

/* timer_callback when a new period starts*/
static void timer_callback(struct timer_list* timer){
	mp2_task_struct* task = from_timer(task, timer, wakeup_timer);
    mutex_lock(&mutex_list);
	pr_info("timer callback: %d, %lu is ready to go. -----wakeup dispatching\n", task->pid, task->period);
	task->state = READY;
    mutex_unlock(&mutex_list);
	wake_up_process(dispatching_thread);
}

/* When a job has been completed, the process called yield to voluntarily give out CPU.*/
static void yield_handler(pid_t pid){
	mp2_task_struct* task;

    mutex_lock(&mutex_list);
	list_for_each_entry(task, &task_list, task_node){
		if(task->pid == pid){
			if(task->next_wakeup == 0){
				// First time yield
				timer_setup(&task->wakeup_timer, timer_callback, 0);
				task->next_wakeup = jiffies + msecs_to_jiffies(task->period);
				pr_info("Initially setup  %d timer!\n", task->pid);
			}
			else{
				task->next_wakeup += msecs_to_jiffies(task->period);
				pr_info("Task %d done. vvvvvvvvvv wake up dispatching.\n", task->pid);
			}

			if(task->next_wakeup > jiffies){
				mod_timer(&task->wakeup_timer, task->next_wakeup);
				task->state = SLEEPING;
				
				// wake up scheduler
				wake_up_process(dispatching_thread);

				
    			mutex_unlock(&mutex_list);
				// put the process into sleep
				set_current_state(TASK_INTERRUPTIBLE);
				schedule();
				return;
			}
			else{
				pr_alert("Over deadlines!\n");
			}
			break;
		}
	}
    mutex_unlock(&mutex_list);
}

static int canBeAdmitted(unsigned long period, unsigned long processing_time){
	unsigned long utilization = 0;
	mp2_task_struct* task;

    mutex_lock(&mutex_list);
	list_for_each_entry(task, &task_list, task_node){
		utilization += task->processing_time * 1000 / task->period;
	}
    mutex_unlock(&mutex_list);

	utilization += processing_time * 1000 / period;
	if(utilization <= 693){
		return 1;
	}
	else{
		return 0;
	}
}

static void register_task(pid_t pid, unsigned long period, unsigned long processing_time){
	// check repeated PID
	mp2_task_struct* task, *new_task;

    mutex_lock(&mutex_list);
	list_for_each_entry(task, &task_list, task_node){
		if(task->pid == pid){
			pr_err("Attempt to register repeated PID %d. Ignoring request.\n", pid);
    		mutex_unlock(&mutex_list);
			return;
		}
	}
    mutex_unlock(&mutex_list);

	// check admission control
	if(!canBeAdmitted(period, processing_time)){
		pr_err("Task cannot be admitted.\n");
		return;
	}


	new_task = kmem_cache_alloc(mp2_task_cache, GFP_KERNEL);
	if(!new_task){
		pr_info("Error allocating from slab cache.\n");
		return;
	}

	new_task->linux_task = find_task_by_pid(pid);
	if(new_task->linux_task == NULL){
		kmem_cache_free(mp2_task_cache, new_task);
		pr_info("Error finding task by pid.\n");
		return;
	}

	new_task->pid = pid;
	new_task->period = period;
	new_task->processing_time = processing_time;
	new_task->state = SLEEPING;
	new_task->next_wakeup = 0; // init


    mutex_lock(&mutex_list);
	list_add_tail(&new_task->task_node, &task_list);
	mutex_unlock(&mutex_list);


	pr_info("Registered PID: %d,%lu,%lu\n", pid, period, processing_time);
}

static void deregister_task(pid_t pid){
	mp2_task_struct *task, *tmp_task;

    mutex_lock(&mutex_list);
	list_for_each_entry_safe(task, tmp_task, &task_list, task_node){
		if(task->pid == pid){
			mutex_lock(&mutex_cur_task);
			if(current_running_task->pid == pid){
				current_running_task = NULL;
			}
			mutex_unlock(&mutex_cur_task);
			del_timer_sync(&task->wakeup_timer);
			list_del(&task->task_node);
			kmem_cache_free(mp2_task_cache, task);
			pr_info("De-registered PID: %d\n", pid);
			break;
		}
	}
    mutex_unlock(&mutex_list);

	mutex_lock(&mutex_cur_task);
	if(current_running_task == NULL){
		wake_up_process(dispatching_thread);
	}
	mutex_unlock(&mutex_cur_task);
}

static ssize_t mp2_status_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
	char *kbuf = kmalloc(count, GFP_KERNEL);
    size_t len=0;
    mp2_task_struct* task;
    
    if(!kbuf){
        return -ENOMEM;
    }

    mutex_lock(&mutex_list);
	list_for_each_entry(task, &task_list, task_node){
        len += snprintf(kbuf + len, count - len, "%d: %lu, %lu\n", task->pid, task->period, task->processing_time);
        // Check if we're out of buffer space
        if (len >= count - 1) {
            break;
        }
    }
    mutex_unlock(&mutex_list);

	if (*ppos >= len) {
        kfree(kbuf);
        return 0;
    }

    if (count > len - *ppos){
        count = len - *ppos;
    }

    if (copy_to_user(buf, kbuf + *ppos, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    *ppos += count;
    kfree(kbuf);
    return count;
}

static ssize_t mp2_status_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos) {
    // handle Registration, Yield, and Deregistration
	char* buf;
	char command;
	int pid;
	unsigned long period, processing_time;

	buf = kmalloc(count +1, GFP_KERNEL);
	if(!buf){
		return -ENOMEM;
	}

	if(copy_from_user(buf, buffer, count)){
		kfree(buf);
		return -EFAULT;
	}

	buf[count] = '\0';

	// Determine which command is passed by the user
	if(sscanf(buf, "%c,%d", &command, &pid) != 2){
		pr_err("Invalid message format.\n");
		kfree(buf);
		return -EINVAL;
	}
	switch(command){
		case 'R':
			if (sscanf(buf, "R,%d,%lu,%lu", &pid, &period, &processing_time) != 3) {
                pr_err("Invalid REGISTRATION message format\n");
				kfree(buf);
                return -EINVAL;
            }
			register_task(pid, period, processing_time);
			break;
		case 'Y':
			yield_handler(pid);
			break;
		case 'D':
			deregister_task(pid);
			break;
		default:
			pr_alert("Invalid command\n");
			kfree(buf);
			return -EINVAL;
	}

	kfree(buf);
	return count;
}

static const struct proc_ops mp2_status_fops = {
    .proc_read = mp2_status_read,
    .proc_write = mp2_status_write
};



// mp2_init - Called when module is loaded
static int __init mp2_init(void)
{
#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADING\n");
#endif
	// Insert your code here ...
	mutex_init(&mutex_list);
	mutex_init(&mutex_cur_task);
	proc_mp2_dir = proc_mkdir(PROC_DIR, NULL);
	if(!proc_mp2_dir){
		pr_err("Error creating /proc/%s directory\n", PROC_DIR);
	}

	proc_mp2_status = proc_create(STATUS_FILE, 0666, proc_mp2_dir, &mp2_status_fops);
	if (!proc_mp2_status) {
        pr_err("Error creating /proc/%s/%s file\n", PROC_DIR, STATUS_FILE);
        remove_proc_entry(PROC_DIR, NULL);
        return -ENOMEM;
    }

	mp2_task_cache = kmem_cache_create("mp2_task_cache", sizeof(mp2_task_struct), 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!mp2_task_cache) {
		printk(KERN_INFO "Error creating slab cache.\n");
		return -ENOMEM;
	}
	pr_info("kmem_cache created.\n");


	dispatching_thread = kthread_create(dispatching_function, NULL, "mp2_dispatching_thread");
	if(IS_ERR(dispatching_thread)){
		pr_info("Error: Cannot create dispatching thread\n");
	}
	else{
		wake_up_process(dispatching_thread);
	}

	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	return 0;
}

// mp2_exit - Called when module is unloaded
static void __exit mp2_exit(void)
{
	mp2_task_struct *task, *tmp_task;
#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
#endif
	// Insert your code here ...
	kthread_stop(dispatching_thread);

    mutex_lock(&mutex_list);
	list_for_each_entry_safe(task, tmp_task, &task_list, task_node){
		wake_up_process(task->linux_task);
		list_del(&task->task_node);
		kmem_cache_free(mp2_task_cache, task);
	}
    mutex_unlock(&mutex_list);
	
	kmem_cache_destroy(mp2_task_cache);
	if (proc_mp2_status) {
        remove_proc_entry(STATUS_FILE, proc_mp2_dir);
    }
    if (proc_mp2_dir) {
        remove_proc_entry(PROC_DIR, NULL);
    }

	mutex_destroy(&mutex_list);
	mutex_destroy(&mutex_cur_task);
	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
