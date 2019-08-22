#include <inc/lib.h>
#include <inc/elf.h>

#define UTEMP2USTACK(addr)	((void*) (addr) + (USTACKTOP - PGSIZE) - UTEMP)
#define UTEMP2			(UTEMP + PGSIZE)
#define UTEMP3			(UTEMP2 + PGSIZE)

// Helper functions for spawn.
static int init_stack(envid_t child, const char **argv, uintptr_t *init_esp);
static int map_segment(envid_t child, uintptr_t va, size_t memsz,
		       int fd, size_t filesz, off_t fileoffset, int perm);
static int copy_shared_pages(envid_t child);

// Spawn a child process from a program image loaded from the file system.
// prog: the pathname of the program to run.
// argv: pointer to null-terminated array of pointers to strings,
// 	 which will be passed to the child as its command-line arguments.
// Returns child envid on success, < 0 on failure.
int
spawn(const char *prog, const char **argv)
{

	// This code follows this procedure:
	//
	//   - Open the program file.
	//
	//   - Read the ELF header, as you have before, and sanity check(完整性检查) its
	//     magic number.  (Check out your load_icode!)
	//
	//   - Use sys_exofork() to create a new environment.
	//
	//   - Set child_tf to an initial struct Trapframe for the child.
	//
	//   - Call the init_stack() function above to set up
	//     the initial stack page for the child environment.
	//
	//   - Map all of the program's segments that are of p_type
	//     ELF_PROG_LOAD into the new environment's address space.
	//     Use the p_flags field in the Proghdr for each segment
	//     to determine how to map the segment:
	//
	//	* If the ELF flags do not include ELF_PROG_FLAG_WRITE,
	//	  then the segment contains text and read-only data.
	//	  Use read_map() to read the contents of this segment,
	//	  and map the pages it returns directly into the child
	//        so that multiple instances of the same program
	//	  will share the same copy of the program text.
	//        Be sure to map the program text read-only in the child.
	//        Read_map is like read but returns a pointer to the data in
	//        *blk rather than copying the data into another buffer.
	//
	//	* If the ELF segment flags DO include ELF_PROG_FLAG_WRITE,
	//	  then the segment contains read/write data and bss.
	//	  As with load_icode() in Lab 3, such an ELF segment
	//	  occupies p_memsz bytes in memory, but only the FIRST
	//	  p_filesz bytes of the segment are actually loaded
	//	  from the executable file - you must clear the rest to zero.
	//        For each page to be mapped for a read/write segment,
	//        allocate a page in the parent temporarily at UTEMP,
	//        read() the appropriate portion of the file into that page
	//	  and/or use memset() to zero non-loaded portions.
	//	  (You can avoid calling memset(), if you like, if
	//	  page_alloc() returns zeroed pages already.)
	//        Then insert the page mapping into the child.
	//        Look at init_stack() for inspiration.
	//        Be sure you understand why you can't use read_map() here.
	//
	//     Note: None of the segment addresses or lengths above
	//     are guaranteed to be page-aligned, so you must deal with
	//     these non-page-aligned values appropriately.
	//     The ELF linker does, however, guarantee that no two segments
	//     will overlap on the same page; and it guarantees that
	//     PGOFF(ph->p_offset) == PGOFF(ph->p_va).
	//
	//   - Call sys_env_set_trapframe(child, &child_tf) to set up the
	//     correct initial eip and esp values in the child.
	//
	//   - Start the child process running with sys_env_set_status().
	unsigned char elf_buf[512];
	struct Trapframe child_tf;
	envid_t child;

	int fd, i, r;
	struct Elf *elf;
	struct Proghdr *ph;
	int perm;

	if ((r = open(prog, O_RDONLY)) < 0)//Open the program file.
		return r;
	fd = r;

	// Read elf header
	elf = (struct Elf*) elf_buf;
	if (readn(fd, elf_buf, sizeof(elf_buf)) != sizeof(elf_buf)
	    || elf->e_magic != ELF_MAGIC) {
		close(fd);
		cprintf("elf magic %08x want %08x\n", elf->e_magic, ELF_MAGIC);
		return -E_NOT_EXEC;
	}

	// Create new child environment
	if ((r = sys_exofork()) < 0)
		return r;
	child = r;

	// Set up trap frame, including initial stack.
	child_tf = envs[ENVX(child)].env_tf;
	child_tf.tf_eip = elf->e_entry;

	// 假设此时argv={"init", "initarg1", "initarg2", NULL}
	if ((r = init_stack(child, argv, &child_tf.tf_esp)) < 0)
		return r;

	// Set up program segments as defined in ELF header.
	ph = (struct Proghdr*) (elf_buf + elf->e_phoff);
	for (i = 0; i < elf->e_phnum; i++, ph++) {
		if (ph->p_type != ELF_PROG_LOAD)
			continue;
		perm = PTE_P | PTE_U;
		if (ph->p_flags & ELF_PROG_FLAG_WRITE)
			perm |= PTE_W;
		if ((r = map_segment(child, ph->p_va, ph->p_memsz,
				     fd, ph->p_filesz, ph->p_offset, perm)) < 0)
			goto error;
	}
	close(fd);
	fd = -1;

	// Copy shared library state.
	if ((r = copy_shared_pages(child)) < 0)
		panic("copy_shared_pages: %e", r);

	child_tf.tf_eflags |= FL_IOPL_3;   // devious: see user/faultio.c
	if ((r = sys_env_set_trapframe(child, &child_tf)) < 0)//设置好了child_tf后再设到env中
		panic("sys_env_set_trapframe: %e", r);

	if ((r = sys_env_set_status(child, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return child;

error:
	sys_env_destroy(child);
	close(fd);
	return r;
}

// Spawn, taking command-line arguments array directly on the stack.
// NOTE: Must have a sentinal of NULL at the end of the args
// (none of the args may be NULL).
// 感觉主要将参数放到argv[]数组中，然后调用spawn
int
spawnl(const char *prog, const char *arg0, ...)
{
	// We calculate argc by advancing the args until we hit NULL.
	// The contract of the function guarantees that the last
	// argument will always be NULL, and that none of the other
	// arguments will be NULL.
	int argc=0;

	// 假设spawnl("/init", "init", "initarg1", "initarg2", (char*)0)
	// 我认为va_list就像一个栈，va_start会将arg0后面的字符串一一入栈，而va_arg则是一个出栈的过程
	// 故va_start之后，vl="initarg1", "initarg2", (char*)0
	// 在经过while循环后，argc=2,因为我想(char *)0应该就是NULL，vl此时应该空了，va_end它
	va_list vl;
	va_start(vl, arg0);
	while(va_arg(vl, void *) != NULL)
		argc++;
	// 因为对参数列表去头去尾，所以本例中argc=2
	va_end(vl);
	// Now that we have the size of the args, do a second pass
	// and store the values in a VLA, which has the format of argv
	const char *argv[argc+2];
	argv[0] = arg0;
	argv[argc+1] = NULL;

	va_start(vl, arg0);
	unsigned i;
	for(i=0;i<argc;i++)
		argv[i+1] = va_arg(vl, const char *);
	va_end(vl);
	// cprintf("the argc in spawnl:%d %s %s %s %s\n",argc, argv[0], argv[1],argv[2],argv[3]);
	// 所以最后argv={"init", "initarg1", "initarg2", NULL}; argc=2因为去头去尾;
	return spawn(prog, argv);
}


// Set up the initial stack page for the new child process with envid 'child'
// using the arguments array pointed to by 'argv',
// which is a null-terminated array of pointers to null-terminated strings.
//
// On success, returns 0 and sets *init_esp
// to the initial stack pointer with which the child should start.
// Returns < 0 on failure.
static int
init_stack(envid_t child, const char **argv, uintptr_t *init_esp)
{
	size_t string_size;
	int argc, i, r;
	char *string_store;
	uintptr_t *argv_store;

	// Count the number of arguments (argc)
	// and the total amount of space needed for strings (string_size).
	string_size = 0;

	// 假设spawnl("/init", "init", "initarg1", "initarg2", (char*)0) 
	// 故argv={"init", "initarg1", "initarg2", NULL}
	// 所以以这为例，argc=3, argc+1=4, 只是只加到argv[2]
	for (argc = 0; argv[argc] != 0; argc++)
		string_size += strlen(argv[argc]) + 1;

	// Determine where to place the strings and the argv array.
	// Set up pointers into the temporary page 'UTEMP'; we'll map a page
	// there later, then remap that page into the child environment
	// at (USTACKTOP - PGSIZE).为什么要搞得这么麻烦？ 因为是先在父环境设好再直接映射物理页到子环境，不然父环境无法操作子环境内存空间
	// strings is the topmost(最顶端) thing on the stack.
	string_store = (char*) UTEMP + PGSIZE - string_size;
	// argv is below that.  There's one argument pointer per argument, plus
	// a null pointer.
	argv_store = (uintptr_t*) (ROUNDDOWN(string_store, 4) - 4 * (argc + 1));

	if ((void*) (argv_store - 2) < (void*) UTEMP)
		return -E_NO_MEM;

	// Allocate the single stack page at UTEMP. 现在是在当前环境的UTEMP上设置好，然后再映射到子环境的用户栈上
	if ((r = sys_page_alloc(0, (void*) UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		return r;


	//	* Initialize 'argv_store[i]' to point to argument string i,
	//	  for all 0 <= i < argc.
	//	  Also, copy the argument strings from 'argv' into the
	//	  newly-allocated stack page.
	//
	//	* Set 'argv_store[argc]' to 0 to null-terminate the args array.
	//
	//	* Push two more words onto the child's stack below 'args',
	//	  containing the argc and argv parameters to be passed
	//	  to the child's umain() function.
	//	  argv should be below argc on the stack.
	//	  (Again, argv should use an address valid in the child's
	//	  environment.)
	//
	//	* Set *init_esp to the initial stack pointer for the child,
	//	  (Again, use an address valid in the child's environment.)
	for (i = 0; i < argc; i++) {
		argv_store[i] = UTEMP2USTACK(string_store);  // =string_store + USTACKTOP - string_size
		strcpy(string_store, argv[i]);
		string_store += strlen(argv[i]) + 1;
	}
	argv_store[argc] = 0;
	assert(string_store == (char*)UTEMP + PGSIZE);

	argv_store[-1] = UTEMP2USTACK(argv_store);
	argv_store[-2] = argc;

	*init_esp = UTEMP2USTACK(&argv_store[-2]);
/* 我认为子环境的用户栈应该会被设成这样的：
	//下面的argv[n]指的是字符串首地址，也是这个栈中对应条目的虚拟地址
	argv[2] -->	|		"initarg2"	|  <--  USTACKTOP 
	argv[1] -->	|		"initarg1"	|
	argv[0] -->	|		"init"		|
			|		 0(NULL)	|
			|		 argv[2]	|
			|		 argv[1]	|
	addr x -->	|		 argv[0]	|
			|	  	 addr x		|
 	child->esp -->  |		   3		| (往上是出栈)
*/
	// After completing the stack, map it into the child's address space
	// and unmap it from ours!
	if ((r = sys_page_map(0, UTEMP, child, (void*) (USTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		goto error;
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		goto error;

	return 0;

error:
	sys_page_unmap(0, UTEMP);
	return r;
}

static int
map_segment(envid_t child, uintptr_t va, size_t memsz,
	int fd, size_t filesz, off_t fileoffset, int perm)
{
	int i, r;
	void *blk;

	//cprintf("map_segment %x+%x\n", va, memsz);

	if ((i = PGOFF(va))) {
		va -= i;
		memsz += i;
		filesz += i;
		fileoffset -= i;
	}

	for (i = 0; i < memsz; i += PGSIZE) {
		if (i >= filesz) {
			// allocate a blank page
			if ((r = sys_page_alloc(child, (void*) (va + i), perm)) < 0)
				return r;
		} else {
			// from file
			if ((r = sys_page_alloc(0, UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
				return r;
			if ((r = seek(fd, fileoffset + i)) < 0)
				return r;
			if ((r = readn(fd, UTEMP, MIN(PGSIZE, filesz-i))) < 0)
				return r;
			if ((r = sys_page_map(0, UTEMP, child, (void*) (va + i), perm)) < 0)
				panic("spawn: sys_page_map data: %e", r);
			sys_page_unmap(0, UTEMP);
		}
	}
	return 0;
}

// Copy the mappings for shared pages into the child address space.复制到相同的内存空间
static int
copy_shared_pages(envid_t child)
{
	// LAB 5: Your code here.
	int r,i;
	for (i = 0; i < PGNUM(USTACKTOP); i ++){ 
		// uvpd、uvpt应该是个全局数组变量，但是数组元素对应的pde、pte具体是什么应该取决于lcr3设置的是哪个环境的内存空间
		if((uvpd[i/1024] & PTE_P) && (uvpt[i] & PTE_P) && (uvpt[i] & PTE_SHARE)){ //i跟pte一一对应，而i/1024就是该pte所在的页表
			if ((r = sys_page_map(0, PGADDR(i/1024, i%1024, 0), child,PGADDR(i/1024, i%1024, 0), uvpt[i] & PTE_SYSCALL)) < 0)
				return r;
		}
	}
	return 0;
	
}

