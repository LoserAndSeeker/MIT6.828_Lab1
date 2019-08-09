#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle; //idle 闲置的

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.通过下面代码来停止CPU

	// LAB 4: Your code here.
	int i=0,j;
	//cprintf("i:%d cpu:%d\n",i,cpunum());
	if(cpus[cpunum()].cpu_env)
		i=ENVX(cpus[cpunum()].cpu_env->env_id);
	//cprintf("i:%d\n",i);
	
	for(j=(i+1)%NENV; j!=i; j=(j+1)%NENV)
		if(envs[j].env_status == ENV_RUNNABLE)
			env_run(&envs[j]);
	//我上面那个循环写的有点问题，如果不加下面这个判断，就少判断了env[i]的状态
	//这样当所有用户环境只剩env[i]的状态是ENV_RUNNABLE时，cpu无法运行env[i]也无法halt，就一直在循环
	if(j==i && envs[j].env_status == ENV_RUNNABLE)
		env_run(&envs[j]);
	
	if(curenv && curenv->env_status == ENV_RUNNING){
		//cpus[cpunum()].cpu_env=&envs[j];
		//cprintf("i:%d cpu:%d id:%08x\n",i,cpunum(),cpus[cpunum()].cpu_env->env_id);
		env_run(curenv);
		//cprintf("i:%d cpu:%d id:%08x\n",i,cpunum(),cpus[cpunum()].cpu_env->env_id);		
	}
	//cprintf("j:%d cpunum:%d env_id:%08x j_id:%08x\n",j,cpunum(),cpus[cpunum()].cpu_env->env_id,envs[j].env_id);
	// sched_halt never returns
	sched_halt();


}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	//能够进来这里，就说明已经没有可操作的env给当前CPU了，所以halt这个CPU
	//可能有些ENV_RUNNABLE或者ENV_RUNNING或者ENV_DYING的都正在其他CPU上
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

