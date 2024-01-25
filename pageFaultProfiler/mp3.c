#define LINUX

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP3");

#define DEBUG 1

#define PROC_DIR "mp3"
#define STATUS_FILE "status"
#define MP3_MAJOR 423
#define MP3_MINOR 0

static int dev_major;
static struct cdev mp3_cdev;

/* Declarations for proc filesystem directories and files */
static struct proc_dir_entry* proc_mp3_dir;
static struct proc_dir_entry* proc_mp3_status;
static struct delayed_work mp3_work;
static unsigned long* profiler_buffer;
#define BUFFER_SIZE (128 * 4096)
#define PROFILER_BUFFER_LEN (128 * 4096 / sizeof(unsigned long))

struct mp3_task_struct  {
	struct list_head node;
	pid_t pid;
	unsigned long utime;
   unsigned long stime;
   unsigned long maj_flt;
   unsigned long min_flt;
};


LIST_HEAD(pcb_list);

static unsigned long profiler_buffer_index = 0;

spinlock_t pcb_list_lock;
spinlock_t buffer_lock;

static void work_function(struct work_struct *work) {
	struct mp3_task_struct *task;
	unsigned long minor_fault = 0, major_fault = 0, cpu_use = 0;

   spin_lock(&pcb_list_lock);
	list_for_each_entry(task, &pcb_list, node) {
		if (get_cpu_use(task->pid, &task->min_flt, &task->maj_flt, &task->utime, &task->stime) != -1) {
			minor_fault += task->min_flt;
			major_fault += task->maj_flt;
			cpu_use += task->utime + task->stime;
		}
	}
   spin_unlock(&pcb_list_lock);

   spin_lock(&buffer_lock);
	// Store the data in the buffer
	profiler_buffer[profiler_buffer_index++] = jiffies; // jiffies
	profiler_buffer[profiler_buffer_index++] = minor_fault; // jiffies
	profiler_buffer[profiler_buffer_index++] = major_fault; // jiffies
	profiler_buffer[profiler_buffer_index++] = cpu_use;; // jiffies
	// pr_info("minflt: %lu, majflt: %lu, cpu_use: %lu", profiler_buffer[profiler_buffer_index-3], profiler_buffer[profiler_buffer_index-2], profiler_buffer[profiler_buffer_index-1]);


   if(profiler_buffer_index >= PROFILER_BUFFER_LEN){
      profiler_buffer_index = 0;
   }
   spin_unlock(&buffer_lock);

	// Reschedule the work
   spin_lock(&pcb_list_lock);
	if (!list_empty(&pcb_list)) {
		schedule_delayed_work(&mp3_work, msecs_to_jiffies(50));
	}
   spin_unlock(&pcb_list_lock);
}

static void register_task(pid_t pid){
   struct mp3_task_struct *task, *new_task;
   spin_lock(&pcb_list_lock);
   list_for_each_entry(task, &pcb_list, node){
		if(task->pid == pid){
			pr_err("Attempt to register repeated PID %d. Ignoring request.\n", pid);
			return;
		}
	}
   spin_unlock(&pcb_list_lock);

   new_task = kmalloc(sizeof(struct mp3_task_struct), GFP_KERNEL);
   if(!new_task){
		pr_info("Error allocating.\n");
		return;
	}
   new_task->pid = pid;
   new_task->utime = 0;
   new_task->stime = 0;
   new_task->maj_flt = 0;
   new_task->min_flt = 0;



   spin_lock(&pcb_list_lock);
   if(list_empty(&pcb_list)){
      INIT_DELAYED_WORK(&mp3_work, work_function);
      schedule_delayed_work(&mp3_work, msecs_to_jiffies(50)); // 50 ms for 20 times per second
   }
   list_add_tail(&new_task->node, &pcb_list);
   spin_unlock(&pcb_list_lock);

}

static void unregister_task(pid_t pid){
   struct mp3_task_struct *task, *tmp_task;
   spin_lock(&pcb_list_lock);
   list_for_each_entry_safe(task, tmp_task, &pcb_list, node){
		if(task->pid == pid){
			list_del(&task->node);
			kfree(task);
			pr_info("Un-registered PID: %d\n", pid);
			break;
		}
	}
   if(list_empty(&pcb_list)){
      cancel_delayed_work_sync(&mp3_work);
   }
   spin_unlock(&pcb_list_lock);
}


static ssize_t mp3_status_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
	char *kbuf = kmalloc(count, GFP_KERNEL);
   size_t len=0;
   struct mp3_task_struct *task;

   // unsigned long index = 0;
   // while(index < profiler_buffer_index){
   //    pr_info("Jiffies: %lu, minflt: %lu, majflt: %lu, cpu_use: %lu\n", profiler_buffer[index], profiler_buffer[index+1], profiler_buffer[index+2], profiler_buffer[index+3]);
   //    index += 4;
   // }
   // pr_info("Profiler index %lu\n", profiler_buffer_index);

   if(!kbuf){
      return -ENOMEM;
   }

   spin_lock(&pcb_list_lock);
   list_for_each_entry(task, &pcb_list, node) {
        len += snprintf(kbuf + len, count - len, "%d\n", task->pid);

        /* Check for buffer overflow */
        if (len >= count-1) {
            break;
        }
    }
   spin_unlock(&pcb_list_lock);


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

static ssize_t mp3_status_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos) {
    // handle Registration, Yield, and Deregistration
	char* buf;
	char command;
	int pid;

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
	if(sscanf(buf, "%c %d", &command, &pid) != 2){
		pr_err("Invalid message format.\n");
		kfree(buf);
		return -EINVAL;
	}
	switch(command){
		case 'R':
			register_task(pid);
			break;
		case 'U':
			unregister_task(pid);
			break;
		default:
			pr_alert("Invalid command\n");
			kfree(buf);
			return -EINVAL;
	}

	kfree(buf);
	return count;
}

static int mp3_chrdev_open(struct inode *inode, struct file *file) {
    return 0; // Empty function
}

static int mp3_chrdev_release(struct inode *inode, struct file *file) {
    return 0; // Empty function
}

static int mp3_chrdev_mmap(struct file *file, struct vm_area_struct *vma) {
   unsigned long start = vma->vm_start;
   unsigned long size = vma->vm_end - vma->vm_start;
   unsigned long page;

   spin_lock(&buffer_lock);
   char *profiler_buffer_ptr = (char*)profiler_buffer;
   spin_unlock(&buffer_lock);

   while (size > 0) {
      page = vmalloc_to_pfn(profiler_buffer_ptr);
      if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED) < 0) {
         return -EAGAIN;
      }
      start += PAGE_SIZE;
      profiler_buffer_ptr += PAGE_SIZE;
      size -= PAGE_SIZE;
   }
   return 0;
}

static struct file_operations mp3_cdev_fops = {
    .open = mp3_chrdev_open,
    .release = mp3_chrdev_release,
    .mmap = mp3_chrdev_mmap
};

static const struct proc_ops mp3_status_fops = {
    .proc_read = mp3_status_read,
    .proc_write = mp3_status_write
};



int __init mp3_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP3 MODULE LOADING\n");
   #endif
   spin_lock_init(&pcb_list_lock);
   spin_lock_init(&buffer_lock);
   // Insert your code here ...
   proc_mp3_dir = proc_mkdir(PROC_DIR, NULL);
	if(!proc_mp3_dir){
		pr_err("Error creating /proc/%s directory\n", PROC_DIR);
	}

	proc_mp3_status = proc_create(STATUS_FILE, 0666, proc_mp3_dir, &mp3_status_fops);
	if (!proc_mp3_status) {
        pr_err("Error creating /proc/%s/%s file\n", PROC_DIR, STATUS_FILE);
        remove_proc_entry(PROC_DIR, NULL);
        return -ENOMEM;
   }

   spin_lock(&buffer_lock);
   profiler_buffer = (unsigned long*)vmalloc(BUFFER_SIZE);
   if (!profiler_buffer) {
        pr_info("Failed to vmalloc.\n");
        return -ENOMEM;
    }

	memset(profiler_buffer, 0xff, BUFFER_SIZE);
   unsigned long p;
   for (p = (unsigned long)profiler_buffer; p < (unsigned long)profiler_buffer + BUFFER_SIZE; p += PAGE_SIZE) {
      SetPageReserved(vmalloc_to_page((void *)p));
   }
   spin_unlock(&buffer_lock);

   dev_t dev;
   dev = MKDEV(MP3_MAJOR, MP3_MINOR);
   dev_major = register_chrdev_region(dev, 1, "mp3_chrdev");
   if (dev_major < 0) {
      pr_info("Failed to register a character device\n");
      return dev_major;
   }
   cdev_init(&mp3_cdev, &mp3_cdev_fops);
   cdev_add(&mp3_cdev, dev, 1);
   
   printk(KERN_ALERT "MP3 MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp3_exit(void)
{
   struct mp3_task_struct *task, *tmp_task;
   #ifdef DEBUG
   printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
   #endif
   // Insert your code here ...
   if (proc_mp3_status) {
        remove_proc_entry(STATUS_FILE, proc_mp3_dir);
   }
   if (proc_mp3_dir) {
      remove_proc_entry(PROC_DIR, NULL);
   }

   spin_lock(&buffer_lock);
   if (profiler_buffer) {
      unsigned long p;
      for (p = (unsigned long)profiler_buffer; p < (unsigned long)profiler_buffer + BUFFER_SIZE; p += PAGE_SIZE) {
         ClearPageReserved(vmalloc_to_page((void *)p));
      }
      vfree(profiler_buffer);
   }
   spin_unlock(&buffer_lock);

   spin_lock(&pcb_list_lock);
   if (!list_empty(&pcb_list)) {
      cancel_delayed_work_sync(&mp3_work);
   }
   list_for_each_entry_safe(task, tmp_task, &pcb_list, node){
		list_del(&task->node);
      kfree(task);
	}
   spin_unlock(&pcb_list_lock);

   cdev_del(&mp3_cdev);
   unregister_chrdev_region(MKDEV(MP3_MAJOR, MP3_MINOR), 1);

   printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);
