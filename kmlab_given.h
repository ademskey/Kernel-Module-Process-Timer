#ifndef __KMLAB_GIVEN_INCLUDE__
#define __KMLAB_GIVEN_INCLUDE__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pid.h>
#include <linux/sched.h>

#define find_task_by_pid(nr) pid_task(find_vpid(nr), PIDTYPE_PID)

int get_cpu_use(int pid, unsigned long *cpu_use);

#endif
