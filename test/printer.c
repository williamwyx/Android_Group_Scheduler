#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>

int main(void) 
{
	int i;

	for (i = 0; i < 100; i++) {
		printf("Hahaha! %d\n", i);
		usleep(1000000);
	}
	return 0;
}
