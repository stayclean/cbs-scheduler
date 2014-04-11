/* cs194-24 Lab 2 */
#define _XOPEN_SOURCE
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <syscall.h>

#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "cbs.h"

#ifndef BOGO_MIPS
#warning "BOGO_MIPS isn't defined, it should be by the build system"
#endif

#define NANO (1000 * 1000 * 1000)
#define MICRO (1000 * 1000)

#define SCHED_CBS_BW 6
#define SCHED_CBS_RT 7

/* FIXME: This is defined in <bits/sigevent.h>, but it doesn't seem to
 * actually be getting used... does anyone know why? */
#define sigev_notify_thread_id _sigev_un._tid

struct cbs_task
{
	pid_t pid;
	int ret;
};
enum cbs_state
{
	CBS_STATE_SLEEP,
	CBS_STATE_RUNNING,
	CBS_STATE_READY,
	CBS_STATE_DONE,
};


int cbs_create(cbs_t *thread, enum cbs_type type,
		size_t cpu, struct timeval *period,
		int (*entry)(void *), void *arg)
{

	int period_usec = period->tv_usec;
	*thread = NULL;

	if (type != CBS_RT && type != CBS_BW) {
		fprintf(stderr, "Invalid cbs_type");
		exit(1);
	}
	if (cpu == 0 || period_usec == 0)
	{
		return EAGAIN;
	}

	struct cbs_task *task = malloc(sizeof(struct cbs_task));
	if (task == NULL)
	{
	        return -1;
	}

	task->pid = fork();
	task->ret = CBS_CONTINUE;
	if (task->pid < 0) {
		free(task);
		return EAGAIN;
	} else if (task->pid == 0)
	{
		// child process executes at entry point
		while(task->ret == CBS_CONTINUE)
		{
			task->ret = entry(arg);
		}
		exit(1);
	} else {
		struct sched_param param = {
			.sched_priority = 2, // do numeric priorities even matter?
			// Note: the scheduling policy of a task available in task->policy
			.cpu_budget = cpu,
			.period_ns = period_usec * 1000
		};

		switch(type){
		case CBS_RT:
			sched_setscheduler(task->pid, SCHED_CBS_RT, &param);
			break;
		case CBS_BW:
			sched_setscheduler(task->pid, SCHED_CBS_BW, &param);
			break;
		default:
			// shouldn't be here..
			break;
		}
		*thread = task;
	}
}

int cbs_join(cbs_t *thread, int *code)
{
	/* Should just call wait */
	struct cbs_task *task = (struct cbs_task *)thread;
	int status, pid, ret;
	pid = waitpid(task->pid, &status, 0); // we could optionally look at status if we want to...
	ret = task->ret;
	free(task);

	return ret;
}
