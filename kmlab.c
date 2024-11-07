#define LINUX

/**************************** Setup  ********************************/
// Include necessary libraries
#include <linux/module.h> // For module init and exit
#include <linux/kernel.h> // For printk
#include "kmlab_given.h" // Given header file
#include <linux/proc_fs.h> // For proc file system
#include <linux/slab.h> // For kmalloc
#include <linux/spinlock.h> // For spinlock
#include <linux/uaccess.h> // For copy_to_user
#include <linux/workqueue.h> // For workqueue
#include <linux/jiffies.h>  // For timing
#include <linux/sched.h>  // For 'task_struct'

// Module Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adam Caudle");
MODULE_DESCRIPTION("CPTS360 Kernel Module Programming PA");

// Global Defenitions
#define PROCFS_MAX_SIZE 1024  // Max size of proc file
#define PROCFS_NAME "kmlab" // Procfs directory name (when created)
#define PROCFS_NAME_FILE "status" // File name in procfs directory
#define TIMER_EXPIRE 10 // Timer expire time
#define MAX_ENT_LEN 32 // Max entry length into kmbuffer
#define DEBUG 1 // Enable debug messages

// Function Prototypes
int __init kmlab_init(void);
void __exit kmlab_exit(void);
ssize_t procfile_read(struct file *file, char __user *buffer, size_t len, loff_t *offset);
ssize_t procfile_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset);
void timer_callback(struct timer_list *t);
void workqueue_callback(struct work_struct *work);
int get_cpu_use(int pid, unsigned long *cpu_use); //Added to get rid of warning
 

/***************************** Structures ********************************************/
// store per process data (you may modify it as needed)
typedef struct {
    struct list_head list;
    unsigned int pid;
    unsigned long cpu_time;
} proc_list;

static const struct proc_ops proc_file_fops = {
    .proc_read = procfile_read,
    .proc_write = procfile_write,
};

/****************************** Global Variables & Resources **************************************/
// pointers used to create entries in proc file system
// Referenced https://devarea.com/linux-kernel-development-creating-a-proc-file-and-interfacing-with-user-space/
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_file;

// kernel linked list head 
// Referenced https://www.oreilly.com/library/view/linux-device-drivers/0596000081/ch10s05.html for list ops
LIST_HEAD(process_list);

// initialize spinlock struct
// Referenced https://www.hitchhikersguidetolearning.com/2021/01/03/spin-lock-initialization-and-use/ for spinlock ops
static DEFINE_SPINLOCK(lock);

// Initialize timer
// Referenced https://embetronicx.com/tutorials/linux/device-drivers/using-kernel-timer-in-linux-device-driver/ for timer ops
static struct timer_list proc_timer;

// Initialize workqueue
// Referenced https://www.oreilly.com/library/view/understanding-the-linux/0596005652/ch04s08.html for workqueue ops
static struct workqueue_struct *workqueue; // Pointer to the workqueue
DECLARE_WORK(my_work, workqueue_callback); // Declare the work structure


/****************************** Proc read/write Functions *****************************************/

// procfile_read - Called when the /proc file is read
ssize_t procfile_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
   // Initialize variables for counting and iterating
   size_t list_count = 0, total_bytes = 0, size = 0;
   proc_list *process;
   ssize_t ret = len;

   // Check if data has been read (offset non-zero)
   if (*offset) {
      return 0;
   }

   // Spinlock so that only one thread/process can access the list at a time
   // iterate through the list and count the number of entries
   spin_lock(&lock);
   list_for_each_entry(process, &process_list, list) {
      list_count++;
      }  
   spin_unlock(&lock);
   
   // calculate and initialize size of buffer
   size = list_count * MAX_ENT_LEN;
   char *kmbuffer = kmalloc(size, GFP_KERNEL);

   if (!kmbuffer) {
      pr_info("Error: kmalloc failed within procfile_read\n");
      return -EFAULT;
   }

   // Iterate over list and and add pids / cpu times to buffer
   spin_lock(&lock);
   list_for_each_entry(process, &process_list, list) {
      total_bytes += snprintf(kmbuffer + total_bytes, size - total_bytes, "PID%d: %lu\n", process->pid, process->cpu_time);
   }
   spin_unlock(&lock);

   // Protect against buffer overflow
   if (total_bytes > len) {
    total_bytes = len;
   }

   // Copy buffer to user space
   if (copy_to_user(buffer, kmbuffer, total_bytes)) {
      pr_info("Error: copy_to_user failed within procfile_read\n");
      kfree(kmbuffer);
      ret = -EFAULT;
   }

   // Update offset and return byte written
   *offset += total_bytes;
   return total_bytes;
}

// procfile_write - Called when the /proc file is written
ssize_t procfile_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
   // Initialize containers and counters
   int status;
   long pid;
   char kmbuffer[PROCFS_MAX_SIZE] = "";
   proc_list *new_proc;

   // copy new pid from user space
   if (copy_from_user(kmbuffer, buffer, len)) {
      pr_info("Error: copy_from_user failed within procfile_write\n");
      return -EFAULT;
    }

   // convert pid to int
   if ((status = kstrtol(kmbuffer, 10, &pid)) != 0) {
      pr_info("Error: kstrtol failed within procfile_write\n");
      return status;
   }

   // Make entry and append to end of list
   spin_lock(&lock);
   new_proc = kzalloc(sizeof(proc_list), GFP_KERNEL); //kzalloc initializes memory to 0
   new_proc->pid = pid;
   INIT_LIST_HEAD(&new_proc->list);
   list_add_tail(&new_proc->list, &process_list);
   spin_unlock(&lock);

   return len;
}

/****************************** Timer & Workqueue Functions *********************************************/
// timer_callback - Called when the timer expires
void timer_callback(struct timer_list *t) {
    // queue work if list is not empty
    if (!list_empty(&process_list)) {
        schedule_work(&my_work);
    }
    
    // setup periodic timer, callback every 5 seconds
    mod_timer(&proc_timer, jiffies + msecs_to_jiffies(5000));
}

// workqueue_callback - Called when the workqueue is scheduled
void workqueue_callback(struct work_struct *work) {
    // iterate through the list and update cpu time
    proc_list *process, *tmp;
    unsigned long cpu_time;

    spin_lock(&lock);
    list_for_each_entry_safe(process, tmp, &process_list, list) {
        pr_info("Checking CPU usage for PID: %d\n", process->pid);
        int result = get_cpu_use(process->pid, &cpu_time);

        if (result != 0) {
            pr_info("Error: get_cpu_use failed for PID %d with return value %d\n", process->pid, result);
            list_del(&process->list);
            kfree(process);
        } else {
            process->cpu_time = cpu_time;
        }
    }
    spin_unlock(&lock);
}

//THIS FUNCTION RETURNS 0 IF THE PID IS VALID AND THE CPU TIME IS SUCCESFULLY RETURNED BY THE PARAMETER CPU_USE. OTHERWISE IT RETURNS -1
int get_cpu_use(int pid, unsigned long *cpu_use)
{
   struct task_struct* task;
   rcu_read_lock();
   task=find_task_by_pid(pid);
   if (task!=NULL)
   {  
	*cpu_use=task->utime;
        rcu_read_unlock();
        return 0;
   }
   else 
   {
     rcu_read_unlock();
     return -1;
   }
}

/****************************** Init and Exit Functions *********************************************/
// kmlab_init - Called when the module is loaded
int __init kmlab_init(void)
{
   #ifdef DEBUG
   pr_info("KMLAB MODULE LOADING\n");
   #endif

   // Create process list head
   INIT_LIST_HEAD(&process_list);

   // initialize timer and workqueue
   timer_setup(&proc_timer, timer_callback, 0); // 0 for private data
   mod_timer(&proc_timer, jiffies + msecs_to_jiffies(5000)); // start 5 second timer
   //workqueue = create_workqueue("kmlab_workqueue");

   // create /proc/kmlab directory
   proc_dir = proc_mkdir("kmlab", NULL);
   if (!proc_dir) {
      pr_info("Error: proc_mkdir failed\n");
      return -ENOMEM;
   }

   // create /proc/kmlab/status file
   proc_file = proc_create("status", 0666, proc_dir, &proc_file_fops);
   if (!proc_file) {
      pr_info("Error: proc_create for status file failed\n");
      proc_remove(proc_dir);
      return -ENOMEM;
   }
   pr_info("KMLAB MODULE LOADED\n");
   return 0;   
}

// kmlab_exit - Called when the module is unloaded
void __exit kmlab_exit(void)
{
   #ifdef DEBUG
   pr_info("KMLAB MODULE UNLOADING\n");
   #endif
   
   // free linked list
   proc_list *process, *tmp;

   list_for_each_entry_safe(process, tmp, &process_list, list) {
      list_del(&process->list);
      kfree(process);
   }

   // free workqueue & timer
   if (workqueue) {
         flush_workqueue(workqueue);
   destroy_workqueue(workqueue);
   }

   del_timer_sync(&proc_timer);

   // delete proc entries
   proc_remove(proc_file);
   proc_remove(proc_dir);

   pr_info("KMLAB MODULE UNLOADED\n");
}

// Register init and exit functions
module_init(kmlab_init);
module_exit(kmlab_exit);
