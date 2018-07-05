/* Notes written by Qian Liu <qianlxc@outlook.com>
  If you find any bug, please contact with me.*/

#include <mmu.h>
#include <error.h>
#include <env.h>
#include <kerelf.h>
#include <sched.h>
#include <pmap.h>
#include <printf.h>

struct Env *envs = NULL;		// All environments
struct Env *curenv = NULL;	        // the current env

static struct Env_list env_free_list;	// Free list

extern Pde *boot_pgdir;
extern char *KERNEL_SP;


/* Overview:
 *  This function is for making an unique ID for every env.
 *
 * Pre-Condition:
 *  Env e is exist.
 *
 * Post-Condition:
 *  return e's envid on success.
 */

u_int mkenvid(struct Env *e)
{
	static u_long next_env_id = 0;

    /*Hint: lower bits of envid hold e's position in the envs array. */
	u_int idx = e - envs;

    /*Hint:  high bits of envid hold an increasing number. */
	return (++next_env_id << (1 + LOG2NENV)) | idx;
}

/* Overview:
 *  Converts an envid to an env pointer.
 *  If envid is 0 , set *penv = curenv;otherwise set *penv = envs[ENVX(envid)];
 *
 * Pre-Condition:
 *  Env penv is exist,checkperm is 0 or 1.
 *
 * Post-Condition:
 *  return 0 on success,and sets *penv to the environment.
 *  return -E_BAD_ENV on error,and sets *penv to NULL.
 */
int envid2env(u_int envid, struct Env **penv, int checkperm)
{
	struct Env *e;

    /* Hint:
     *  If envid is zero, return the current environment.*/
	if (envid == 0) {
		*penv = curenv;
		return 0;
	}

	e = &envs[ENVX(envid)];

	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*penv = 0;
		return -E_BAD_ENV;
	}

    /* Hint:
     *  Check that the calling environment has legitimate permissions
     *  to manipulate the specified environment.
     *  If checkperm is set, the specified environment
     *  must be either the current environment.
     *  or an immediate child of the current environment. */

    if (checkperm && e != curenv && e->env_parent_id != curenv->env_id){
        *penv = 0;
        return -E_BAD_ENV;
    }
	*penv = e;
	return 0;
}

/* Overview:
 *  Mark all environments in 'envs' as free and insert them into the env_free_list.
 *  Insert in reverse order,so that the first call to env_alloc() return envs[0].
 *  
 * Hints:
 *  You may use these defines to make it:
 *      LIST_INIT,LIST_INSERT_HEAD
 */
void
env_init(void)
{
	int i;
    /*Step 1: Initial env_free_list. */
	LIST_INIT(&env_free_list);

    /*Step 2: Travel the elements in 'envs', init every element(mainly initial its status, mark it as free)
     * and inserts them into the env_free_list as reverse order. */
	for(i = NENV - 1;i >= 0;i--)
	{
		envs[i].env_status = ENV_FREE;
		LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
	}

}


/* Overview:
 *  Initialize the kernel virtual memory layout for environment e.
 *  Allocate a page directory, set e->env_pgdir and e->env_cr3 accordingly,
 *  and initialize the kernel portion of the new environment's address space.
 *  Do NOT map anything into the user portion of the environment's virtual address space.
 */
/***Your Question Here***/
static int
env_setup_vm(struct Env *e)
{

	int i, r;
	struct Page *p = NULL;
	Pde *pgdir;

    /*Step 1: Allocate a page for the page directory and add its reference.
     *pgdir is the page directory of Env e. */
	if ((r = page_alloc(&p)) < 0) {
		panic("env_setup_vm - page_alloc error\n");
		return r;
	}
	p->pp_ref++;
	pgdir = (Pde *)page2kva(p);
    
    /*Step 2: Zero pgdir's field before UTOP. */
	for (i = 0; i < PDX(UTOP); i++) {
		pgdir[i] = 0;
	}

    /*Step 3: Copy kernel's boot_pgdir to pgdir. */

    /* Hint:
     *  The VA space of all envs is identical above UTOP
     *  (except at VPT and UVPT, which we've set below).
     *  See ./include/mmu.h for layout.
     *  Can you use boot_pgdir as a template?
     */
	for (i = PDX(UTOP); i <= PDX(~0); i++) {
		pgdir[i] = boot_pgdir[i];
	}
	e->env_pgdir = pgdir;
	e->env_cr3   = PADDR(pgdir);

    /*Step 4: VPT and UVPT map the env's own page table, with
     *different permissions. */
    e->env_pgdir[PDX(VPT)]   = e->env_cr3;
	//printf("pa:%x, base:%x delta:%x pgdir[0x%x]:%x\n", page2pa(p), e->env_pgdir, PDX(UVPT) * 4, PDX(UVPT), e->env_pgdir + PDX(UVPT));
    e->env_pgdir[PDX(UVPT)]  = e->env_cr3 | PTE_V | PTE_R;
	return 0;
}

/* Overview:
 *  Allocates and Initializes a new environment.
 *  On success, the new environment is stored in *new.
 *
 * Pre-Condition:
 *  If the new Env doesn't have parent, parent_id should be zero.
 *  env_init has been called before this function.
 *
 * Post-Condition:
 *  return 0 on success, and set appropriate values for Env new.
 *  return -E_NO_FREE_ENV on error, if no free env.
 *
 * Hints:
 *  You may use these functions and defines:
 *      LIST_FIRST,LIST_REMOVE,mkenvid (Not All)
 *  You should set some states of Env:
 *      id , status , the sp register, CPU status , parent_id
 *      (the value of PC should NOT be set in env_alloc)
 */

int
env_alloc(struct Env **new, u_int parent_id)
{
	int r;
	struct Env *e;
    
    /*Step 1: Get a new Env from env_free_list*/
	e = LIST_FIRST(&env_free_list);
	if(!e)
		return -E_NO_FREE_ENV;

    
    /*Step 2: Call certain function(has been implemented) to init kernel memory layout for this new Env.
     *The function mainly maps the kernel address to this new Env address. */
	if((r = env_setup_vm(e)) < 0)
		return r;


    /*Step 3: Initialize every field of new Env with appropriate values*/
	e->env_id = mkenvid(e);
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;


    /*Step 4: focus on initializing env_tf structure, located at this new Env. 
     * especially the sp register,CPU status. */
    e->env_tf.cp0_status = 0x10001004;
	if(parent_id)
		e->env_tf.cp0_status = 0x10001004;
	e->env_tf.regs[29] = USTACKTOP;


    /*Step 5: Remove the new Env from Env free list*/
	LIST_REMOVE(e, env_link);
	*new = e;
	//printf("envid:%d, pgdir:%x\n", e->env_id, e->env_pgdir);
	return 0;

}

/* Overview:
 *   This is a call back function for kernel's elf loader.
 * Elf loader extracts each segment of the given binary image.
 * Then the loader calls this function to map each segment
 * at correct virtual address.
 *
 *   `bin_size` is the size of `bin`. `sgsize` is the
 * segment size in memory.
 *
 * Pre-Condition:
 *   va aligned 4KB and bin can't be NULL.
 *
 * Post-Condition:
 *   return 0 on success, otherwise < 0.
 */
static int load_icode_mapper(u_long va, u_int32_t sgsize,
							 u_char *bin, u_int32_t bin_size, void *user_data)
{
	struct Env *env = (struct Env *)user_data;
	struct Page *p = NULL;
	u_long i;
	int r;
	u_long offset = va - ROUNDDOWN(va, BY2PG);
	
	//printf("[[load_icode_mapper start]]\n");
	//printf("load_icode_mapper(va:%x, sgsize:0x%x, bin_size:0x%x)\n", va, sgsize, bin_size);
	//printf("bin:%x, size:0x%x\n", bin, bin_size);
	

	/*Step 1: load all content of bin into memory. */
	//Hint: What if va isn't aligned to 4KB? Actually, IT MAY NOT ALIGN TO 4KB.
	if(offset)
	{
		if((r = page_alloc(&p)) < 0)
			return r;
		page_insert(env->env_pgdir, p, ROUNDDOWN(va, BY2PG), PTE_R);
		i = BY2PG - offset;
		if(i > bin_size)
		{
			bcopy(bin, page2kva(p) + offset, bin_size);
			bin_size = 0;
			sgsize = sgsize > i?sgsize - i:0;
		}
		else
		{
			bcopy(bin, page2kva(p) + offset, i);
			bin_size -= i;
			sgsize -= i;
		}
		bin += i;
		va = ROUND(va, BY2PG);
	}
	//printf("after aligned:\n");
	//printf("load_icode_mapper(va:%x, sgsize:0x%x, bin_size:0x%x)\n", va, sgsize, bin_size);
	for (i = 0; i < bin_size; i += BY2PG) {
		/* Hint: You should alloc a page and increase the reference count of it. */
		//printf("try to alloc a page\n");
		if((r = page_alloc(&p)) < 0)
			return r;
		//printf("done\n");
		//printf("try to insert a page\n");
		//printf("in pgdir (kva)%x, map (va)%x to (pa)%x, size:0x%x\n", env->env_pgdir, va + i, page2pa(p), BY2PG);
		page_insert(env->env_pgdir, p, va + i, PTE_R);
		//printf("done\n");
		if(i + BY2PG <= bin_size)
			bcopy(bin + i, page2kva(p), BY2PG);
		else
			bcopy(bin + i, page2kva(p), bin_size - i);
	}
	//printf("part 1 done\n");
	/*Step 2: alloc pages to reach `sgsize` when `bin_size` < `sgsize`.
    * i has the value of `bin_size` now. */
	while (i < sgsize) {
		if((r = page_alloc(&p)) < 0)
			return r;
		page_insert(env->env_pgdir, p, va + i, PTE_R);
		i += BY2PG;
	}
	//printf("**exit load_icdoe_mapper**\n");
	return 0;
}
/* Overview:
 *  Sets up the the initial stack and program binary for a user process.
 *  This function loads the complete binary image by using elf loader,
 *  into the environment's user memory. The entry point of the binary image
 *  is given by the elf loader. And this function maps one page for the
 *  program's initial stack at virtual address USTACKTOP - BY2PG.
 *
 * Hints: 
 *  All mappings are read/write including those of the text segment.
 *  You may use these :
 *      page_alloc, page_insert, page2kva , e->env_pgdir and load_elf.
 */
static void
load_icode(struct Env *e, u_char *binary, u_int size)
{
	/* Hint:
	 *  You must figure out which permissions you'll need
	 *  for the different mappings you create.
	 *  Remember that the binary image is an a.out format image,
	 *  which contains both text and data.
     */
	struct Page *p = NULL;
	u_long entry_point;
	u_long r;
    u_long perm;
    
	//printf("[load_icode start]\n");
    /*Step 1: alloc a page. */
	//printf("try to alloc a page for stack\n");
	if((r = page_alloc(&p)) < 0)
		return r;
	//printf("done\n");


    /*Step 2: Use appropriate perm to set initial stack for new Env. */
    /*Hint: The user-stack should be writable? */
	//printf("try to map the phys page to va of stack\n");
	page_insert(e->env_pgdir, p, USTACKTOP - BY2PG, PTE_R);
	//printf("done\n");


    /*Step 3:load the binary by using elf loader. */
	//printf("try to load elf\n");
	load_elf(binary, size, &entry_point, e, load_icode_mapper);
	//printf("done\n");


    /***Your Question Here***/
    /*Step 4:Set CPU's PC register as appropriate value. */
	//printf("set pc %x\n", entry_point);
	e->env_tf.pc = entry_point;
	//printf("done\n");
	//printf("*exit load_icode*\n");
}

/* Overview:
 *  Allocates a new env with env_alloc, loads te named elf binary into 
 *  it with load_icode. This function is ONLY called during kernel
 *  initialization, before running the first user_mode environment.
 *
 * Hints: 
 *  this function wrap the env_alloc and load_icode function.
 */
void
env_create(u_char *binary, int size)
{
	struct Env *e;
    /*Step 1: Use env_alloc to alloc a new env. */
	int r;
	//printf("try to alloc a env\n");
	if((r = env_alloc(&e, 0)) < 0)
		return r;
	//printf("create a env with id %d\n", e->env_id);
	//printf("done\n");


    /*Step 2: Use load_icode() to load the named elf binary. */
	//printf("[env_create]try to load elf\n");
	load_icode(e, binary, size);
	//printf("done\n");


}

/* Overview:
 *  Frees env e and all memory it uses.
 */
void
env_free(struct Env *e)
{
	Pte *pt;
	u_int pdeno, pteno, pa;

    /* Hint: Note the environment's demise.*/
	printf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

    /* Hint: Flush all mapped pages in the user portion of the address space */
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {
        /* Hint: only look at mapped page tables. */
		if (!(e->env_pgdir[pdeno] & PTE_V)) {
			continue;
		}
        /* Hint: find the pa and va of the page table. */
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (Pte *)KADDR(pa);
        /* Hint: Unmap all PTEs in this page table. */
		for (pteno = 0; pteno <= PTX(~0); pteno++)
			if (pt[pteno] & PTE_V) {
				page_remove(e->env_pgdir, (pdeno << PDSHIFT) | (pteno << PGSHIFT));
			}
        /* Hint: free the page table itself. */
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}
    /* Hint: free the page directory. */
	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));
    /* Hint: return the environment to the free list. */
	e->env_status = ENV_FREE;
	LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

/* Overview:
 *  Frees env e, and schedules to run a new env 
 *  if e is the current env.
 */
void
env_destroy(struct Env *e)
{
    /* Hint: free e. */
	env_free(e);

    /* Hint: schedule to run a new environment. */
	if (curenv == e) {
		curenv = NULL;
        /* Hint:Why this? */
		bcopy((void *)KERNEL_SP - sizeof(struct Trapframe),
			  (void *)TIMESTACK - sizeof(struct Trapframe),
			  sizeof(struct Trapframe));
		printf("i am killed ... \n");
		sched_yield();
	}
}

extern void env_pop_tf(struct Trapframe *tf, int id);
extern void lcontext(u_int contxt);

/* Overview:
 *  Restores the register values in the Trapframe with the
 *  env_pop_tf, and context switch from curenv to env e.
 *
 * Post-Condition:
 *  Set 'e' as the curenv running environment.
 *
 * Hints:
 *  You may use these functions:
 *      env_pop_tf and lcontext.
 */
void
env_run(struct Env *e)
{
	/*Step 1: save register state of curenv. */
    /* Hint: if there is a environment running,you should do
    *  context switch.You can imitate env_destroy() 's behaviors.*/
	//printf("begin to run env %d\n", e->env_id);
	if(curenv)
	{
		bcopy((void *)TIMESTACK - sizeof(struct Trapframe),
				&(curenv->env_tf),
				sizeof(struct Trapframe));
		curenv->env_tf.pc = curenv->env_tf.cp0_epc;
	}
	
    /*Step 2: Set 'curenv' to the new environment. */
	curenv = e;
	curenv->env_runs += 1;
	//printf("e->pc:%x\n", e->env_tf.pc);

    /*Step 3: Use lcontext() to switch to its address space. */
	//printf("%d's env_pgdir:%x\n", e->env_id, e->env_pgdir);
	//printf("%d's pc:%x\n", e->env_id, e->env_tf.pc);
	lcontext(e->env_pgdir);

    /*Step 4: Use env_pop_tf() to restore the environment's
     * environment   registers and drop into user mode in the
     * the   environment.
     */
    /* Hint: You should use GET_ENV_ASID there.Think why? */
	//printf("last\n");
	env_pop_tf(&(e->env_tf), GET_ENV_ASID(e->env_id));

}
