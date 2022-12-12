#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include <round.h>
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


/*
* *유효주소 여부 확인
* 유저가상주소 할당 OR user stack범위내부
*/
bool
check_valid_addr(void * addr){
	struct thread *curr = thread_current();
	struct page *page;
	// printf("====check_valid_addr\n");
	// printf("fault_addr : %X f->rsp : %X thread_current()->stack_pointer : %X\n", addr, curr->tf.rsp, thread_current()->stack_pointer);
	if ((addr
			&& is_user_vaddr(addr)
			&& ( (page = spt_find_page(&curr->spt, addr)) != NULL) 
				||(addr <= USER_STACK && addr >= USER_STACK - 0x100000  && addr >= pg_round_down(curr->stack_pointer)) )){
		return true;
	}
	else
		return false;
}

/*
* *버퍼 유효성 확인
* writable체크(가상주소만, 스택X), 유효주소여부확인
*/
bool
check_valid_buffer(void* buffer, unsigned size, bool need_writable){
	// offset 정렬
	struct thread *curr = thread_current();
	if (pg_ofs(buffer) != 0){
		size += (buffer - pg_round_down(buffer));
		buffer = pg_round_down(buffer);
	}
	ASSERT(pg_ofs(buffer) == 0); //정렬 여부 확인
	while(1){
		// 유효주소 확인
		if (!check_valid_addr(buffer))
			return false;
		// writable 확인
		struct page* page = spt_find_page(&curr->spt, buffer);
		if(page!= NULL && !page->writable && need_writable){
			return false;
		}
		if(size < PGSIZE){
			break; 
		}
		buffer += PGSIZE;
		size -= PGSIZE;
	}
	return true;
}

/*
* *mmap 유효성 확인
* 페이지 전체 SPT확인
*/
bool
check_valid_mmap(void* addr, unsigned size){
	unsigned addr_size = ROUND_UP(size, PGSIZE);

	while(addr_size!=0){
		// printf("addr : %X, addr_size :%u\n", addr, addr_size);
		if(spt_find_page(&thread_current()->spt, addr)){
			return false;
		}
		addr_size -= PGSIZE;
		addr += PGSIZE;
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
	if (!check_valid_addr(filename))
	{
		curr->my_exit_code = -1;
		thread_exit();
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
	// return -1;
	struct thread *curr = thread_current();
	if (!check_valid_addr(filename))
	{
		curr->my_exit_code = -1;
		thread_exit();
	}
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
		thread_current()->my_exit_code = -1;
		thread_exit();
	}
	else if (f_table[fd]){
		lock_acquire(&filesys_lock);
		file_close(f_table[fd]);
		lock_release(&filesys_lock);
		f_table[fd] = NULL;
	}
	else{
		thread_current()->my_exit_code = -1;
		thread_exit();
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
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL || !(check_valid_buffer(buffer, size, true)))
	{
		thread_current()->my_exit_code = -1;
		thread_exit();
	}
	struct file *f = curr->fd_table[fd];
	// printf("read_handler fd : %d file:%X, buffer :%X, size :%d\n", fd, f, buffer, size);
	lock_acquire(&filesys_lock);
	result = file_read(f, buffer, size);
	lock_release(&filesys_lock);
	return result;
}

int sys_write_handler(int fd, void *buffer, unsigned size){
	struct thread *curr = thread_current(); 
	int result; 
	// printf("write_handler,fd: %d buffer : %X\n",fd, buffer);
	// if (fd == 1 && (check_valid_buffer(buffer, size, false))) // 이부분 확인 필요!!
	if (fd == 1) // 이부분 확인 필요!!
	{
		// lock_acquire(&filesys_lock);
		putbuf(buffer, size);
		// lock_release(&filesys_lock);ƒ
		// printf("write_fd_end ,fd: %d buffer : %X\n",fd, buffer);
		return size;
	}
	


	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL || buffer == NULL || !(check_valid_buffer(buffer, size, false))) 
	// if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL || buffer == NULL || !(check_valid_addr(buffer)))
	{	
		curr->my_exit_code = -1;
		thread_exit();
	}
	struct file *f = curr->fd_table[fd];
	lock_acquire(&filesys_lock);
	result = file_write(f, buffer, size);
	lock_release(&filesys_lock);
	return result;
}

int sys_fork_handler(char *thread_name, struct intr_frame *f){
	// printf("fork start\n");
	return process_fork(thread_name, f);
}

int sys_wait_handler(int pid){
	// printf("wait start\n");
	return process_wait(pid);
}

int sys_exec_handler(char * cmd_line){
	// printf("exec start\n");
	struct thread *curr = thread_current();
	if (!check_valid_addr(cmd_line))
	{
		curr->my_exit_code = -1;
		thread_exit();
	}
 	char *fn_copy = palloc_get_page (0);
	strlcpy (fn_copy, cmd_line, PGSIZE);
	return process_exec(fn_copy);
}

void 
sys_seek_handler(int fd, unsigned position){
	struct thread *curr = thread_current ();
	struct file **f_table = curr->fd_table;
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL) {
		curr->my_exit_code = -1;
		thread_exit();
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
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL) {
		curr->my_exit_code = -1;
		thread_exit();
	}
	struct file *f = f_table[fd];
	lock_acquire(&filesys_lock);
	file_tell(f);
	lock_release(&filesys_lock);
}

void *sys_mmap_handler(void* addr, size_t length, int writable, int fd, off_t offset) {
	// printf("mmap : addr: %X length : %u writable :%d fd :%d offset: %d\n", addr, length, writable, fd, offset);
	struct thread *curr = thread_current ();
	struct file **fd_table = curr->fd_table;
	if (fd < FDBASE || fd >= FDLIMIT || curr->fd_table[fd] == NULL
		|| length == 0 ){
		curr->my_exit_code = -1;
		thread_exit();
	}
	struct file *file = fd_table[fd];
	if (addr==NULL || pg_ofs(addr)!=0 || pg_ofs(offset) != 0 
		|| is_kernel_vaddr(addr)
		|| ((size_t)addr+(size_t)length) == 0 // 커널 검증코드 추후 확인 필요
		|| !check_valid_mmap(addr, length)){
			// printf("addr+length:%u\n",(size_t)addr+(size_t)length);
			// printf("Y/N :%d\n", ((size_t)addr+(size_t)length) >= KERN_BASE);
			return NULL;
	}
	do_mmap(addr, length, writable, file, offset);
	return addr;
}

void sys_munmap_handler(void* addr){
	struct thread *curr = thread_current ();
	struct page *page = spt_find_page(&curr->spt, addr); 
	// printf("munmap addr:%X file :%X offset :%d, r_b ; %d, z_b : %d\n", addr, page->file.file, page->file.offset, page->file.page_read_bytes, page->file.page_zero_bytes);
	// FIXME: VM_TYPE 체크
	if (!addr|| !page || VM_TYPE(page->operations->type)!= VM_FILE){
		// ||page->file.file->deny_write == true){
		// || !page->file.is_start){
		curr->my_exit_code = -1;
		thread_exit();
	}
	do_munmap(addr);
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
	default:
		break;
	}
}


void
printf_hash2(struct supplemental_page_table *spt){
	struct hash *h = &spt->page_hash;
	struct hash_iterator i;
   	hash_first (&i, h);
	printf("===== hash 순회시작 =====\n");
   	while (hash_next (&i))
   	{
		struct page *p = hash_entry(hash_cur(&i), struct page, hash_elem);
		if (p->frame == NULL){
			printf("va: %X, writable : %d\n",p->va, p->writable);
		}
		else {
			printf("va: %X, kva : %X writable : %d\n",p->va, p->frame->kva, p->writable);
		}
   	}
	printf("===== hash 순회종료 =====\n");
}