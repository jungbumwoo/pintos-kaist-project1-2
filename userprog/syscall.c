#include "userprog/syscall.h" //
#include <stdio.h> //
#include <syscall-nr.h> // 
#include "threads/interrupt.h" //
#include "threads/thread.h" //
#include "threads/loader.h" // 
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// add
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// void check_address(uaddr);
void check_address(const uint64_t *uaddr);
// void check_valid_buffer(void *buffer, unsigned size, void *rsp, bool to_write);

void halt (void);
void exit (int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);

int _write (int fd UNUSED, const void *buffer, unsigned size);


void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int dup2(int oldfd, int newfd);

tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (char *file_name);

// Project 2-4 File Descriptor
static struct file *find_file_by_fd(int fd);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);
// int exec (const char *cmd_line);

/* Project2-extra */
const int STDIN = 1;
const int STDOUT = 2;

// Project3 
static void check_writable_addr(void* ptr);

void *mmap_s (void *addr, size_t length, int writable, int fd, off_t offset);


// temp

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

	// Project 2-4. File descriptor
	lock_init(&file_rw_lock);
	lock_init(&syscall_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	// intr_frame 에서 stack pointer 를 get
	// stack(esp) 에서 system call num를 get.

	/*
	이런 식으로 스택에서 찾아와 빼서 쓰면 됨
	arg3
	arg2
	arg1
	arg0
	num
	esp
	*/

	struct thread* curr = thread_current ();
	curr->stack_bottom = f->rsp;


	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
			exit(-1);
		break;
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		// check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		// check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_DUP2:
		f->R.rax = dup2(f->R.rdi, f->R.rsi);
		break;
	case SYS_MMAP:
		f->R.rax = mmap_s(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap(f->R.rdi);
		break;
	default:
		exit(-1);
		break;
	}

	// printf ("system call!\n");
	// thread_exit ();
	/*
		procedure syscall_handler (interrupt frame)
			get stack pointer from interrupt frame
			get system call number from stack
			switch (system call number){
			case the number is halt:
			call halt function;
			break;
			case the number is exit:
			call exit function;
			break;
			…
			default
			call thread_exit function;
			}
	*/
}

void check_address(const uint64_t *uaddr)
{
	struct thread *cur = thread_current();
	// if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4_get_page(cur->pml4, uaddr) == NULL)
	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4e_walk(cur->pml4, uaddr, 0) == NULL)
	{
		exit(-1);
	}
	struct page *page = spt_find_page (&thread_current() -> spt, uaddr);
	if (page == NULL) exit(-1);
}

// void check_address(const uint64_t *uaddr)
// {
// 	/* 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */

// 	/* 잘못된 접근일 경우 프로세스 종료 */

// 	/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
// 	Pintos에서는 시스템 콜이 접근할 수 있는 주소를 0x8048000~0xc0000000으로 제한함
// 유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
// 	/* ref) userprog/pagedir.c, threads/vaddr.h */
// 	struct thread *cur = thread_current();
// 	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4_get_page(cur->pml4, uaddr) == NULL)
// 	{
// 		exit(-1);
// 	}
// }

// void check_valid_buffer(void *buffer, unsigned size, void *rsp, bool to_write)
// {
// 	/* 인자로 받은 buffer부터 buffer + size까지의 크기가 한 페이지의
// 	크기를 넘을 수도 있음 */
// 	/* check_address를 이용해서 주소의 유저영역 여부를 검사함과 동시
// 	에 vm_entry 구조체를 얻음 */
// 	/* 해당 주소에 대한 vm_entry 존재여부와 vm_entry의 writable 멤
// 	버가 true인지 검사 */
// 	/* 위 내용을 buffer 부터 buffer + size까지의 주소에 포함되는
// 	vm_entry들에 대해 적용 */
// 	for (int i = 0; i < size; i++)
// 	{
// 		struct page *page = check_address(buffer + i);
// 		if (page == NULL)
// 			exit(-1);
// 		if (to_write == true && page->writable == false) // to_write 
// 			exit(-1);
// 	}
// }

// void get_argument(void *esp, int *arg , int count)
// {
// 	/* 유저 스택에 저장된 인자값들을 커널로 저장 */
// 	/* 인자가 저장된 위치가 유저영역인지 확인 */

// 	/* 유저 스택에 있는 인자들을 커널에 저장하는 함수
// 	스택 포인터(esp)에 count(인자의 개수) 만큼의 데이터를 arg에 저장 */
// }

void halt (void)
{
	/* shutdown_power_off()를 사용하여 pintos 종료 */	
	power_off();
}

void exit (int status)
{
	/* 실행중인 스레드 구조체를 가져옴 */
	/* 프로세스 종료 메시지 출력,
	출력 양식: “프로세스이름: exit(종료상태)” */
	/* 스레드 종료 */
	struct thread *cur = thread_current();
	cur->exit_status = status;

	// printf("%s: exit(%d)\n", thread_name(), status); // Process Termination Message
	thread_exit();
}

bool create(const char *file, unsigned initial_size)
{
	/* 파일 이름과 크기에 해당하는 파일 생성 */
	/* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */

	check_address(file);
	lock_acquire(&file_rw_lock);
	bool result = filesys_create(file, initial_size);
	lock_release(&file_rw_lock);
	return result;
}

bool remove(const char *file)
{
	/* 파일 이름에 해당하는 파일을 제거 */
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	check_address(file);
	lock_acquire(&file_rw_lock);
	bool result = filesys_remove(file);
	lock_release(&file_rw_lock);
	return result;
}

int open(const char *file)
{
	check_address(file);
	struct file *fileobj = filesys_open(file);

	if (fileobj == NULL)
		return -1;

	int fd = add_file_to_fdt(fileobj);

	// FD table full
	lock_acquire(&file_rw_lock);
	if (fd == -1)
		file_close(fileobj);
	lock_release(&file_rw_lock);
	return fd;
}

// Returns the size, in bytes, of the file open as fd.
int filesize(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return -1;
	return file_length(fileobj);
}

// Reads size bytes from the file open as fd into buffer.
// Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	check_writable_addr(buffer);

	int ret;
	struct thread *cur = thread_current();

	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return -1;

	if (fileobj == STDIN)
	{
		if (cur->stdin_count == 0)
		{
			// Not reachable
			NOT_REACHED();
			remove_file_from_fdt(fd);
			ret = -1;
		}
		else
		{
			int i;
			unsigned char *buf = buffer;
			for (i = 0; i < size; i++)
			{
				char c = input_getc();
				*buf++ = c;
				if (c == '\0')
					break;
			}
			ret = i;
		}
	}
	else if (fileobj == STDOUT)
	{
		ret = -1;
	}
	else{
		lock_acquire(&file_rw_lock);
		ret = file_read(fileobj, buffer, size);
		lock_release(&file_rw_lock);
	}
	return ret;
}

// Writes size bytes from buffer to the open file fd.
// Returns the number of bytes actually written, or -1 if the file could not be written
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int ret;

	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return -1;

	struct thread *cur = thread_current();
	
	if (fileobj == STDOUT)
	{
		if(cur->stdout_count == 0)
		{
			//Not reachable
			NOT_REACHED();
			remove_file_from_fdt(fd);
			ret = -1;
		}
		else
		{
			putbuf(buffer, size);
			ret = size;
		}
	}
	else if (fileobj == STDIN)
	{
		ret = -1;
	}
	else
	{
		lock_acquire(&file_rw_lock);
		ret = file_write(fileobj, buffer, size);
		lock_release(&file_rw_lock);
	}

	return ret;
}

// Changes the next byte to be read or written in open file fd to position,
// expressed in bytes from the beginning of the file (Thus, a position of 0 is the file's start).
void seek(int fd, unsigned position)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj <= 2)
		return;
	fileobj->pos = position;	
}

// Returns the position of the next byte to be read or written in open file fd, expressed in bytes from the beginning of the file.
unsigned tell(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj <= 2)
		return;
	return file_tell(fileobj);
}

void close(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return;

	struct thread *cur = thread_current();

	// extra 라고함 - 홍욱형
	if (fd == 0 || fileobj == STDIN)
	{
		cur->stdin_count--;
	}
	else if (fd == 1 || fileobj == STDOUT)
	{
		cur->stdout_count--;
	}
	// extra 라고함 - 홍욱형

	remove_file_from_fdt(fd);
	if (fd <= 1 || fileobj <= 2)
		return;

	if (fileobj -> dupCount == 0)
		file_close(fileobj);
	else
		fileobj->dupCount--;
}

// Creates 'copy' of oldfd into newfd. If newfd is open, close it. Returns newfd on success, -1 on fail (invalid oldfd)
// After dup2, oldfd and newfd 'shares' struct file, but closing newfd should not close oldfd (important!)
int dup2(int oldfd, int newfd)
{
	if (oldfd == newfd)
		return newfd;

	struct file *fileobj = find_file_by_fd(oldfd);
	if (fileobj == NULL)
		return -1;

	// struct file *deadfile = find_file_by_fd(newfd);

	struct thread *cur = thread_current();
	struct file **fdt = cur->fdTable;

	// Don't literally copy, but just increase its count and share the same struct file
	// [syscall close] Only close it when count == 0

	// Copy stdin or stdout to another fd
	if (fileobj == STDIN)
		cur->stdin_count++;
	else if (fileobj == STDOUT)
		cur->stdout_count++;
	else
		fileobj->dupCount++;

	close(newfd);
	fdt[newfd] = fileobj;
	return newfd;
}


tid_t fork (const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

// int exec (const char *cmd_line);
int exec (char *file_name)
{
	struct thread *cur = thread_current(); // 이건 왜 넣었지?
	check_address(file_name);


	int siz = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		exit(-1);
	strlcpy(fn_copy, file_name, siz);

	if (process_exec(fn_copy) == -1)
		return -1;

	// Not reachable
	NOT_REACHED();

	// 동적할당된거 free안시켜줘도 되나 - process_exec 넘어가서 해줌
	return 0;
}

// temp
int _write (int fd UNUSED, const void *buffer, unsigned size) {
	// temporary code to pass args related test case
	putbuf(buffer, size);
	return size;
}

// Project 2-4. File descriptor
// Check if given fd is valid, return cur->fdTable[fd]
static struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();

	// Error - invalid id
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return NULL;
	
	return cur->fdTable[fd];	// automatically returns NULL if empty
}

// Find open spot in current thread's fdt and put file in it. Returns the fd.
int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdTable;	// file descriptor table

	// Project2-extra - (multi-oom) Find open spot from the front
	while (cur->fdIdx < FDCOUNT_LIMIT && fdt[cur->fdIdx])
		cur->fdIdx++;

	// Error - fdt full
	if (cur->fdIdx >= FDCOUNT_LIMIT)
		return -1;

	fdt[cur->fdIdx] = file;
	return cur->fdIdx;
}

// Check for valid fd and do cur -> fdTable[fd] = NULL. Returns nothing
void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();

	// Error - invalid fd
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	cur->fdTable[fd] = NULL;
}

static void
check_writable_addr(void* ptr){
	struct page *page = spt_find_page (&thread_current() -> spt, ptr);
	if (page == NULL || !page->writable) exit(-1);
}

void munmap (void *addr){
	do_munmap(addr);	
}



void *mmap_s (void *addr, size_t length, int writable, int fd, off_t offset){
	/* 
	A call to mmap may fail if the file opened as fd has a length of zero bytes. 
	파일의 	byte 수가 0은 아닌지
	It must fail if addr is not page-aligned 
	or if the range of pages mapped overlaps any existing set of mapped pages, 
	including the stack or pages mapped at executable load time. 


	In Linux, if addr is NULL, the kernel finds an appropriate address at which to create the mapping. 
	For simplicity, you can just attempt to mmap at the given addr. 

	Therefore, if addr is 0, it must fail, because some Pintos code assumes virtual page 0 is not mapped. 
	Your mmap should also fail when length is zero. 

	Finally, the file descriptors representing console input and output are not mappable.
	Memory-mapped pages should be also allocated in a lazy manner just like anonymous pages. 
	You can use vm_alloc_page_with_initializer or vm_alloc_page to make a page object.
	*/

	if (addr == 0 || (!is_user_vaddr(addr))) return NULL;
	if ((uint64_t)addr % PGSIZE != 0) return NULL;
	if (offset % PGSIZE != 0) return NULL;
	if ((uint64_t)addr + length == 0) return NULL;
	if (!is_user_vaddr((uint64_t)addr + length)) return NULL;
	for (uint64_t i = (uint64_t) addr; i < (uint64_t) addr + length; i += PGSIZE){
		if (spt_find_page (&thread_current() -> spt, (void*) i)!=NULL) return NULL;
	}

	if (length == 0) return NULL;
	struct file* file = find_file_by_fd(fd);
	if(file == NULL) return NULL;

	return do_mmap(addr, length, writable, file, offset);
}

