#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

bool
check_address(bool writable, char *ptr) {
	if (ptr == NULL)
	{
		return false ;
	}
	struct thread *curr = thread_current();
    struct page *p = spt_find_page(&curr->spt, pg_round_down(ptr));
    if (p == NULL) {
        return false;
    } else {
		if (writable && !p->writable)
		{
			return false;
		}
	}
	return true;
}

void
sys_halt_handler(){
	power_off();
}

void
sys_exit_handler(int arg1){
	thread_current()->my_exit_code = arg1;
	thread_exit();
}

bool sys_create_handler(char *filename, unsigned intial_size){
	bool result;
	struct thread *curr = thread_current();
	if (!check_address(false, filename)){
		sys_exit_handler(-1);
	}
	lock_acquire(&filesys_lock);
	result = filesys_create(filename, intial_size);
	lock_release(&filesys_lock);
	return result;
}

bool sys_remove_handler(char *filename){
	bool result;
	lock_acquire(&filesys_lock);
	result = filesys_remove(filename);
	lock_release(&filesys_lock);
	return result;
}

int sys_open_handler(char *filename){
	struct thread *curr = thread_current();
	if (!check_address(false, filename))
		sys_exit_handler(-1);
	lock_acquire(&filesys_lock);
	struct file *file = filesys_open(filename);
	lock_release(&filesys_lock);
	if (!file)
		return -1;

	struct file **f_table = curr->fd_table;
	int i = FDBASE;
	for (i; i < FDLIMIT; i++)
	{
		if (f_table[i] == NULL){
			f_table[i] = file;
			return i;
		}
	}
	lock_acquire(&filesys_lock);
	file_close(file);
	lock_release(&filesys_lock);
	return -1;
}

int sys_close_handler(int fd){
	struct file **f_table = thread_current()->fd_table;
	if (fd < FDBASE || fd >= FDLIMIT){
		sys_exit_handler(-1);
	}
	else if (f_table[fd]){
		lock_acquire(&filesys_lock);
		file_close(f_table[fd]);
		lock_release(&filesys_lock);
		f_table[fd] = NULL;
	}
	else{
		sys_exit_handler(-1);
	}
}

int sys_filesize_handler(int fd){
	int result;
	struct thread *curr = thread_current();
	struct file **f_table = curr->fd_table;
	struct file *f = f_table[fd]; 
	lock_acquire(&filesys_lock);
	result = file_length(f);
	lock_release(&filesys_lock);
	return result;
}

int sys_read_handler(int fd, void* buffer, unsigned size){
	struct thread *curr = thread_current();
	int result;
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL || !check_address(true, buffer))
	{
		sys_exit_handler(-1);
	}
	struct file *f = curr->fd_table[fd];
	lock_acquire(&filesys_lock);
	result = file_read(f, buffer, size);
	lock_release(&filesys_lock);
	return result;
}

int sys_write_handler(int fd, void *buffer, unsigned size){
	struct thread *curr = thread_current();
	int result;
	if (fd == 1){
		putbuf(buffer, size);
		return size;
	}
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL || !check_address(false, buffer))
	{
		sys_exit_handler(-1);
	}
	struct file *f = curr->fd_table[fd];
	lock_acquire(&filesys_lock);
	result = file_write(f, buffer, size);
	lock_release(&filesys_lock);
	return result;
}

int sys_fork_handler(char *thread_name, struct intr_frame *f){
	return process_fork(thread_name, f);
}

int sys_wait_handler(int pid){
	return process_wait(pid);
}

int sys_exec_handler(char * cmd_line){
	struct thread *curr = thread_current();
	if (!check_address(false, cmd_line)){
		sys_exit_handler(-1);
	}
	char *fn_copy = palloc_get_page(0);
	strlcpy (fn_copy, cmd_line, PGSIZE);
	return process_exec(fn_copy);
}

void 
sys_seek_handler(int fd, unsigned position){
	struct thread *curr = thread_current ();
	struct file **f_table = curr->fd_table;
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL) {
		sys_exit_handler(-1);
	}
	struct file *f = f_table[fd];
	lock_acquire(&filesys_lock);
	file_seek(f, position);
	lock_release(&filesys_lock);
	
}

unsigned
sys_tell_handler(int fd){
	struct thread *curr = thread_current ();
	struct file **f_table = curr->fd_table;
	unsigned result;
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL){
		sys_exit_handler(-1);
	}
	struct file *f = f_table[fd];
	lock_acquire(&filesys_lock);
	result = file_tell(f);
	lock_release(&filesys_lock);
	return result;
}

void *
sys_mmap_handler(void *addr, size_t length, int writable, int fd, off_t offset){
	struct thread *curr = thread_current ();
	struct file **f_table = curr->fd_table;
	struct supplemental_page_table *spt = &curr->spt;
	void* result;
	if (is_kernel_vaddr (addr) || is_kernel_vaddr (length) ||
      is_kernel_vaddr (addr + length)) {
		return NULL;
	}
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL
		|| addr <= NULL || addr != pg_round_down(addr) 
		|| is_kernel_vaddr(addr) || is_kernel_vaddr (addr + length)
		|| (offset % PGSIZE ) != 0 || length <= 0 || length >= 0x8004000000
		|| file_length(f_table[fd])<= 0|| spt_find_page(spt, pg_round_down(addr))){
		return NULL;
	}
	result = do_mmap(addr, length, writable, f_table[fd], offset);
	return result;
}

void 
sys_munmap_handler(void *addr){
	ASSERT(addr == pg_round_down(addr));
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;
	int page_cnt;
	if ((page = spt_find_page(spt, addr)) == NULL || VM_TYPE(page->uninit.type) != VM_FILE || (page_cnt = page->page_cnt) == 0)
	{
		return;
	}
	do_munmap(addr);

	/* remove frome mmap_list */
	struct list_elem *e;
	struct list *m_list = &spt->mmap_list;

	for (e = list_begin(m_list); e != list_end(m_list); e = list_next(e)){
		struct page *p = list_entry(e, struct page, mmap_elem);
		if(p->va == addr)
			break;
	}

	list_remove(e);
	ASSERT(addr == pg_round_down(addr));

	/* remove page from SPT */
	for (int i = 0; i < page_cnt; i++){
		page = spt_find_page(spt, addr);
		spt_remove_page(spt, page);
		addr += PGSIZE;
	}
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) { 
	// TODO: Your implementation goes here.
	int syscall_n = f->R.rax;
	thread_current()->stack_pointer = f->rsp;
	switch (syscall_n)
	{
	case SYS_HALT:
		sys_halt_handler();
		break;
	case SYS_EXIT:
		sys_exit_handler(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = sys_create_handler(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = sys_remove_handler(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = sys_open_handler(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = sys_filesize_handler(f->R.rdi);
		break;
	case SYS_CLOSE:
		f->R.rax = sys_close_handler(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = sys_read_handler(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = sys_write_handler(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_FORK:
		f->R.rax = sys_fork_handler(f->R.rdi, f);
		break;
	case SYS_WAIT:
		f->R.rax = sys_wait_handler(f->R.rdi);
		break;
	case SYS_EXEC:
		sys_exec_handler(f->R.rdi);
		break;
	case SYS_SEEK:
		sys_seek_handler(f->R.rdi,f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = sys_tell_handler(f->R.rdi);
		break;
	case SYS_MMAP:
		f->R.rax = sys_mmap_handler(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		sys_munmap_handler(f->R.rdi);
		break;

	default:
		break;
	}
}
