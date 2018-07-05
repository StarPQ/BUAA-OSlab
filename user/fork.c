// implement fork from user space

#include "lib.h"
#include <mmu.h>
#include <env.h>


/* ----------------- help functions ---------------- */

/* Overview:
 * 	Copy `len` bytes from `src` to `dst`.
 *
 * Pre-Condition:
 * 	`src` and `dst` can't be NULL. Also, the `src` area 
 * 	 shouldn't overlap the `dest`, otherwise the behavior of this 
 * 	 function is undefined.
 */
void user_bcopy(const void *src, void *dst, size_t len)
{
	void *max;

		//writef("~~~~~~~~~~~~~~~~ src:%x dst:%x len:%x\n",(int)src,(int)dst,len);
	max = dst + len;

	// copy machine words while possible
	if (((int)src % 4 == 0) && ((int)dst % 4 == 0)) {
		while (dst + 3 < max) {
			*(int *)dst = *(int *)src;
			dst += 4;
			src += 4;
		}
	}

	// finish remaining 0-3 bytes
	while (dst < max) {
		*(char *)dst = *(char *)src;
		dst += 1;
		src += 1;
	}

	//for(;;);
}

/* Overview:
 * 	Sets the first n bytes of the block of memory 
 * pointed by `v` to zero.
 * 
 * Pre-Condition:
 * 	`v` must be valid.
 *
 * Post-Condition:
 * 	the content of the space(from `v` to `v`+ n) 
 * will be set to zero.
 */
void user_bzero(void *v, u_int n)
{
	char *p;
	int m;

	p = v;
	m = n;

	while (--m >= 0) {
		*p++ = 0;
	}
}
/*--------------------------------------------------------------*/

/* Overview:
 * 	Custom page fault handler - if faulting page is copy-on-write,
 * map in our own private writable copy.
 * 
 * Pre-Condition:
 * 	`va` is the address which leads to a TLBS exception.
 *
 * Post-Condition:
 *  Launch a user_panic if `va` is not a copy-on-write page.
 * Otherwise, this handler should map a private writable copy of 
 * the faulting page at correct address.
 */
static void
pgfault(u_int va)
{
	u_int *tmp;
	u_long perm;
	//writef("pgfault va:0x%x, pgdir:0x%x\n", va, *vpd);
	if(!((perm = (* vpt)[VPN(va)] & 0xfff) & PTE_COW))
		user_panic("not a copy-on-write page.");

	perm ^= PTE_COW;
	//writef("in pgfault\n");
    
    //map the new page at a temporary place
	syscall_mem_alloc(0, USTACKTOP, perm);

	//copy the content
	//writef("pgfault va:%x\n", va);
	//writef("copy");
	user_bcopy(va & ~(0xfff), USTACKTOP, BY2PG);
	
    //map the page on the appropriate place
	syscall_mem_map(0, USTACKTOP, 0, va, perm);
	
    //unmap the temporary place
	syscall_mem_unmap(0, USTACKTOP);
	//writef("out pgfault\n");
	
}

/* Overview:
 * 	Map our virtual page `pn` (address pn*BY2PG) into the target `envid`
 * at the same virtual address. 
 *
 * Post-Condition:
 *  if the page is writable or copy-on-write, the new mapping must be 
 * created copy on write and then our mapping must be marked 
 * copy on write as well. In another word, both of the new mapping and
 * our mapping should be copy-on-write if the page is writable or 
 * copy-on-write.
 * 
 * Hint:
 * 	PTE_LIBRARY indicates that the page is shared between processes.
 * A page with PTE_LIBRARY may have PTE_R at the same time. You
 * should process it correctly.
 */
static void
duppage(u_int envid, u_int pn)
{
	/* Note:
	 *  I am afraid I have some bad news for you. There is a ridiculous, 
	 * annoying and awful bug here. I could find another more adjectives 
	 * to qualify it, but you have to reproduce it to understand 
	 * how disturbing it is.
	 * 	To reproduce this bug, you should follow the steps bellow:
	 * 	1. uncomment the statement "writef("");" bellow.
	 * 	2. make clean && make
	 * 	3. lauch Gxemul and check the result.
	 * 	4. you can add serveral `writef("");` and repeat step2~3.
	 * 	Then, you will find that additional `writef("");` may lead to
	 * a kernel panic. Interestingly, some students, who faced a strange 
	 * kernel panic problem, found that adding a `writef("");` could solve
	 * the problem. 
	 *  Unfortunately, we cannot find the code which leads to this bug,
	 * although we have debugged it for serveral weeks. If you face this
	 * bug, we would like to say "Good luck. God bless."
	 */
	u_int addr;
	u_int perm;
	
	addr = pn * BY2PG;
	//CHECK:vpt or vpd?
	perm = (* vpt)[pn] & 0xfff;
	//writef("duppage 0x%x\n", addr);

	if((perm & PTE_COW) || (perm & PTE_R) && !(perm & PTE_LIBRARY))
		perm |= PTE_COW;
	syscall_mem_map(0, addr, envid, addr, perm);
	syscall_mem_map(0, addr, 0, addr, perm);

	//	user_panic("duppage not implemented");
}

/* Overview:
 * 	User-level fork. Create a child and then copy our address space
 * and page fault handler setup to the child.
 *
 * Hint: use vpd, vpt, and duppage.
 * Hint: remember to fix "env" in the child process!
 * Note: `set_pgfault_handler`(user/pgfault.c) is different from 
 *       `syscall_set_pgfault_handler`. 
 */
extern void __asm_pgfault_handler(void);
int
fork(void)
{
	// Your code here.
	u_int newenvid;
	extern struct Env *envs;
	extern struct Env *env;
	u_int i;

		//The parent installs pgfault using set_pgfault_handler
	set_pgfault_handler(pgfault);

	//writef("fork begin\n");
	newenvid = syscall_env_alloc();
	if(!newenvid)
	{
		env = envs + ENVX(syscall_getenvid());
	}
	else
	{
		//writef("newenvid: %d, envs:0x%x\n", newenvid, envs);
		//writef("env->id: %d, newenv->id:%d\n", env->env_id, (envs + ENVX(newenvid))->env_id);
		//writef("env->pc: %x, newenv->pc:%x\n", env->env_tf.pc, (envs + ENVX(newenvid))->env_tf.pc);
		//writef("env->pgdir: %x, newenv->pgdir:%x\n", env->env_pgdir, (envs + ENVX(newenvid))->env_pgdir);
		//writef("env->epc: %d, newenv->epc:%d\n", env->env_tf.cp0_epc, (envs + ENVX(newenvid))->env_tf.cp0_epc);
		//writef("curenv->env_pgfault_handler:%x\n", env->env_pgfault_handler);

		//alloc a new alloc
		//writef("father begin to duppage\n");
		//writef("vpt:%x, vpd:%x\n", *vpt, *vpd);
		for(i = 0;i < PPN(USTACKTOP - BY2PG);)
		{
			if(!((u_long)(* vpd)[i>>10] & PTE_V))
				i += 1024;
			else
			{
				if((u_long)(* vpt)[i] & PTE_V)
					duppage(newenvid, i);
				i += 1;
				//page_fault_handler
			}
		}

		syscall_set_env_status(newenvid, ENV_RUNNABLE);
	}

	return newenvid;
}

// Challenge!
int
sfork(void)
{
	user_panic("sfork not implemented");
	return -E_INVAL;
}
