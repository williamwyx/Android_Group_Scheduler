#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>

void set_cpu_group_test()
{
	int sys_call_result = syscall(378, 1, 1);
	printf("Result of sched_set_cpu group is: %d\n", sys_call_result);
}

void change_sched_class()
{
	struct sched_param grr = {0};
	//int sched_value;
	
	/* this gives back 0, so its SCHED_NORMAL */
	printf("What's my current schedule? %d\n",
	       sched_getscheduler(getpid()));
	/* Something like this? */
	sched_setscheduler(0, 6, &grr);
	while(1);
	/* printf("Result %d\n", sched_value); */
	/* printf("What's my current schedule? %d\n", */
	/*        sched_getscheduler(getpid())); */
}

int main(void) 
{
	set_cpu_group_test();
	return 0;
}
