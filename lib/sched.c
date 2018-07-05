#include <env.h>
#include <pmap.h>
#include <printf.h>

/* Overview:
 *  Implement simple round-robin scheduling.
 *  Search through 'envs' for a runnable environment ,
 *  in circular fashion statrting after the previously running env,
 *  and switch to the first such environment found.
 *
 * Hints:
 *  The variable which is for counting should be defined as 'static'.
 */
void sched_yield(void)
{
	static int pos = 0;
	//printf("TIMER IRQ\n");
	for(pos++;;pos++)
	{
		if(pos == 6)
			pos = 0;
		if(envs[pos].env_status == ENV_RUNNABLE)
			break;
	}
	//printf("begin to run %x\n", envs[pos].env_id);
	env_run(envs + pos);
}
