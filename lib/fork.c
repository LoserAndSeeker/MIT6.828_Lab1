// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	
	// LAB 4: Your code here.
	/*pte_t *pte;
	pte = pgdir_walk(thisenv->env_pgdir, addr, 0);傻了吧，不能用的*/
	if(!((err & FEC_WR)&&(uvpt[PGNUM(addr)] & (PTE_W | PTE_COW))))
		panic("pgfault conditions wrong!\n");
	//cprintf("utf_fault_va:%08x thisenv_id:%d env_id:%d\n",addr,thisenv->env_id,sys_getenvid());
	//thisenv->env_id跟sys_getenvid()还真不一样。。。难道我fork里thisenv更新错了？

	envid_t envid = sys_getenvid();
	r = sys_page_alloc(envid, (void *)PFTEMP, PTE_P|PTE_W|PTE_U);
	if(r!=0) 
		panic("page alloc fault: %e",r);
	addr = ROUNDDOWN(addr, PGSIZE); //这里也是没想到
	memcpy((void *)PFTEMP, (const void *)addr, PGSIZE);
	r=sys_page_map(envid, (void *)PFTEMP, envid, (void *)addr, PTE_P|PTE_W|PTE_U);
	if(r!=0) 
		panic("page map fault: %e",r);
	r=sys_page_unmap(envid, (void *)PFTEMP); //忘了还得解除映射
	if(r!=0) 
		panic("page unmap fault: %e",r);
	// LAB 4: Your code here.

	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	//pte_t *pte;
	//pte = pgdir_walk(thisenv->env_pgdir, (const void *)pn*PGSIZE, 0);用不了这个函数
	void *addr=(void *)(pn<<12);
	envid_t fu_id = sys_getenvid();
	if(uvpt[pn] & (PTE_W | PTE_COW)){		
		r = sys_page_map(fu_id, (void *)addr, envid, (void *)addr, PTE_COW|PTE_U);
		if(r!=0)
			return r;
		//父进程这里要重新映射一遍，区别是之前有可能是writable，现在只能是COW，so?
		//难道是因为现在父子进程都映射着同一个物理页，如果父进程还是可写的话，就会影响子进程
		r = sys_page_map(fu_id, (void *)addr, fu_id, (void *)addr, PTE_COW|PTE_U);
		if(r!=0) 
			return r;
	}else{
		r = sys_page_map(fu_id, (void *)addr, envid, (void *)addr, uvpt[pn] & PTE_SYSCALL);
		if(r!=0)
			return r;
	}
	
	// LAB 4: Your code here.
	
	return 0;
	panic("duppage not implemented");
	return 0;


}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   把分页机制想象成从CR3开始的三重箭头：pd = lcr3(); pt = *(pd+4*PDX); page = *(pt+4*PTX);
//   JOS中页目录表条目V=0x3BD。 当PTX=PDX=V时，virtual address对应的pte = uvpd(user virtual page directory)
//   当PTX=V && PDX!=V时，virtual address对应的pte = uvpt(user virtual page table)
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   任何用户异常堆栈都不应该标记为写时复制
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t who;
	int i,r;
	set_pgfault_handler(pgfault);
	// fork a child process
	who = sys_exofork();
	if (who < 0)
		panic("sys_exofork: %e", who);
	
	if (who == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// Copy our address space to the child.
	// 父进程虚拟地址空间UTOP以下的每一页都应该在子进程中有所映射
	// 关键是要找到该虚拟地址在页表中对应的pte，才能知道权限
	// 这是在用户空间，page_walk是内核空间的，所以巧妙的通过uvpt、uvpd来找到pte、pde
	// 我们可以知道，uvpd是有1024个pde的一维数组，而uvpt是有2^20个pte的一维数组,与物理页号刚好一一对应
	for (i = 0; i < PGNUM(USTACKTOP); i ++){ 
		//这里只需要到USTACKTOP，后面只有UXSTACKTOP，不能是COW，必须分配物理内存
		//不然老报错[00001000] user_mem_check assertion failure for va eebfffcc
		//报错的位置应该是page_fault_handler()里的user_mem_asser()检查用户异常栈权限
		if((uvpd[i/1024] & PTE_P) && (uvpt[i] & PTE_P)){ //i跟pte一一对应，而i/1024就是该pte所在的页表
			r=duppage(who, i); //区分是不是COW与W都放到duppage中。我之前这里也区分也是乱了
			if(r!=0)
				panic("duppage fault:%e\n",r);
		}
	}

	//   Neither user exception stack should ever be marked copy-on-write,
	//   任何用户异常堆栈都不应该标记为写时复制
	//   so you must allocate a new page for the child's user exception stack.
	r=sys_page_alloc(who, (void *)(UXSTACKTOP-PGSIZE), PTE_W|PTE_U);
	if(r!=0)
		panic("page alloc fault:%e\n",r);
	
	//Copy page fault handler setup 
	extern void _pgfault_upcall(void);
	r=sys_env_set_pgfault_upcall(who, _pgfault_upcall);
	if(r!=0)
		panic("set pgfault upcall fault:%e\n",r);

	// Then mark the child as runnable and return.
	r=sys_env_set_status(who, ENV_RUNNABLE);
	if(r!=0)
		panic("env set status fault:%e\n",r);
	return who; //return 0 我居然在fork里一直返回0？？？
	panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
