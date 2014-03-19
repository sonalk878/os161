/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <uio.h>
#include <kern/iovec.h>
#include <process.h>
#include <synch.h>
#include <spl.h>
#include <addrspace.h>
#include <copyinout.h>
#include <filesupport.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <test.h>
#include <kern/wait.h>

/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */
void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int64_t retval64;
	bool retval_is_32 = true;	// Indicates if the return values is a 32 bit or 64 bit.
	int err;
	off_t arg64_1 = 0;			
	int whence = 0;

	//kprintf("Inside syscall.\n");
	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);
	//kprintf("Past kasserts.\n");

	callno = tf->tf_v0;

	if(callno == SYS_lseek) {
		// Create the 64-bit arguments, in case we need them.
		//kprintf("arg2 = %d, arg3 = %d.\n", tf->tf_a2, tf->tf_a3);
		//arg64_1 = ((off_t) tf->tf_a2) + ((off_t) tf->tf_a3 * 4294967296);
		// Need to OR and shift in order, since variables are only 32-bit.
		arg64_1 |= tf->tf_a2;
		arg64_1 = arg64_1 << 32;
		arg64_1 |= tf->tf_a3;
		// Copy in the whence argument from userland.
		err = copyin((const_userptr_t)(tf->tf_sp+16), &whence, sizeof(whence));
		if (err) {
			kprintf("Bad mem point.\n");
		}
		//kprintf("Whence = %d., arg61_1 = %ld.\n", whence, (long int)arg64_1);
	}

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	struct thread *cur = curthread;
	(void)cur;
	
	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	    case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0,
				 (userptr_t)tf->tf_a1);
		break;

		/*Open*/
		case SYS_open:
			err = sys_open((char*) tf->tf_a0, (int) tf->tf_a1, &retval);
		break;

		/*Write*/
		case SYS_write:
			err = sys_write((int) tf->tf_a0, (const void*) tf->tf_a1, (size_t) tf->tf_a2, &retval);
		break;

		/*Read*/
		case SYS_read:
			err = sys_read((int) tf->tf_a0, (const void*) tf->tf_a1, (size_t) tf->tf_a2, &retval);
			break;
		
		/*Close*/
		case SYS_close:
			err = sys_close((int) tf->tf_a0);
			break;

		/*lseek*/
		case SYS_lseek:
			err = sys_lseek((int) tf->tf_a0, (off_t) arg64_1, (int) whence, &retval64);
			retval_is_32 = false;
			break;

		/* FD redirect */
		case SYS_dup2:
			err = sys_dup2((int) tf->tf_a0, (int) tf->tf_a1, &retval);
			break;

		/* Change Directory */
		case SYS_chdir:
			err = sys_chdir((const char*) tf->tf_a0, &retval);
			break;

		/* Get CWD */
		case SYS___getcwd:
			err = sys___getcwd((char*) tf->tf_a0, (size_t) tf->tf_a1, &retval);
			break;

		case SYS_remove:
			err = sys_remove((const char*) tf->tf_a0, &retval);
			break;

		case SYS_getpid:
			err = sys_getpid(&retval);
			break;

		case SYS__exit:
			sys_exit((int) tf->tf_a0);
			break;

		case SYS_waitpid:
			err = sys_waitpid((int) tf->tf_a0, (int*) tf->tf_a1, (int) tf->tf_a2, &retval); 	
 			break;

 		case SYS_fork:
 			err = sys_fork(tf, &retval);
 			break;

 		case SYS_execv:
 			err = sys_execv((const char*) tf->tf_a0, (char**) tf->tf_a1, &retval);
 			break;

	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      				/* signal an error */
	}
	else {
		/* Success. */
		if (retval_is_32){
			tf->tf_v0 = retval; 			/* retval, if appropriate for syscall */
		}
		else {
			tf->tf_v0 = retval64 >> 32;		// High order bits put in v0
			tf->tf_v1 = retval64;			// Low order bits in v1
		}
		tf->tf_a3 = 0;      				/* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked process.
 *
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
void
enter_forked_process(void *tf, unsigned long parentpid)
{
	// struct process *parent = get_process((pid_t) parentpid);
	// P(parent->p_forksem);
	as_activate(curthread->t_addrspace);
	(void)parentpid;
	struct trapframe newtf;
	struct trapframe *oldtf = tf;
	newtf.tf_vaddr = oldtf->tf_vaddr;	
	newtf.tf_status = oldtf->tf_status;	
	newtf.tf_cause = oldtf->tf_cause;	
	newtf.tf_lo = oldtf->tf_lo;
	newtf.tf_hi = oldtf->tf_hi;
	newtf.tf_ra = oldtf->tf_ra;		/* Saved register 31 */
	newtf.tf_at = oldtf->tf_at;		/* Saved register 1 (AT) */
	newtf.tf_v0 = oldtf->tf_v0;		/* Saved register 2 (v0) */
	newtf.tf_v1 = oldtf->tf_v1;		/* etc. */
	newtf.tf_a0 = oldtf->tf_a0;
	newtf.tf_a1 = oldtf->tf_a1;
	newtf.tf_a2 = oldtf->tf_a2;
	newtf.tf_a3 = oldtf->tf_a3;
	newtf.tf_t0 = oldtf->tf_t0;
	newtf.tf_t1 = oldtf->tf_t1;
	newtf.tf_t2 = oldtf->tf_t2;
	newtf.tf_t3 = oldtf->tf_t3;
	newtf.tf_t4 = oldtf->tf_t4;
	newtf.tf_t5 = oldtf->tf_t5;
	newtf.tf_t6 = oldtf->tf_t6;
	newtf.tf_t7 = oldtf->tf_t7;
	newtf.tf_s0 = oldtf->tf_s0;
	newtf.tf_s1 = oldtf->tf_s1;
	newtf.tf_s2 = oldtf->tf_s2;
	newtf.tf_s3 = oldtf->tf_s3;
	newtf.tf_s4 = oldtf->tf_s4;
	newtf.tf_s5 = oldtf->tf_s5;
	newtf.tf_s6 = oldtf->tf_s6;
	newtf.tf_s7 = oldtf->tf_s7;
	newtf.tf_t8 = oldtf->tf_t8;
	newtf.tf_t9 = oldtf->tf_t9;
	newtf.tf_k0 = oldtf->tf_k0;		/* dummy (see exception.S comments) */
	newtf.tf_k1 = oldtf->tf_k1;		/* dummy */
	newtf.tf_gp = oldtf->tf_gp;
	newtf.tf_sp = oldtf->tf_sp;
	newtf.tf_s8 = oldtf->tf_s8;
	newtf.tf_epc = oldtf->tf_epc;
	newtf.tf_epc += 4;
	newtf.tf_v0 = 0;
	newtf.tf_a3 = 0;
	kfree(tf);
	// memcpy(newtf, tf, sizeof(struct trapframe));
	// (void)junk;
	// kprintf("Entering Forked Process: %d\n",curthread->t_pid);
	// (void)parent;
	// newtf_tf_v0 = 0;
	// newtf_tf_a3 = 0;
	// newtf->tf_epc +=4;
	mips_usermode(&newtf);
}

static struct vnode* console;
static char conname[] = "con:";
static struct vnode* null_dev;
static char nulname[] = "null:";

/* Initialization Functions */

/* Get the Console vnode once, so we dont't have to later */
int
console_init()
{
	int result;

	DEBUG(DB_WRITE,"Getting console vnode...");

	// No need for a lock, since we are doing this during boot time.
	// Create the console device.
	result = vfs_open(conname,O_RDWR,0,&console);
	KASSERT(console != NULL);
	KASSERT(result == 0);
	
	// Create the null device.
	result = vfs_open(nulname,O_RDONLY,0,&null_dev);
	KASSERT(null_dev != NULL);
	KASSERT(result == 0);

	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);

	// Create all the handles and the console file object.
	struct file_object *fo_con = fo_create(conname);
	struct file_object *fo_nul = fo_create(nulname);
	struct file_handle *fh_stdin = fh_create(conname, O_RDONLY);
	struct file_handle *fh_stdout = fh_create(conname, O_WRONLY);
	struct file_handle *fh_stderr = fh_create(conname, O_WRONLY);	

	fo_con->fo_vnode = console;
	fo_nul->fo_vnode = null_dev;

	file_object_list[0] = fo_con;
	file_object_list[1] = fo_nul;

	// Link all the file handles to the same console file object.
	fh_stdin->fh_file_object = fo_con;
	fh_stdout->fh_file_object = fo_con;
	fh_stderr->fh_file_object = fo_con;

	// Link all the file handles to the appropriate file descriptor in the first process.
	// Hope these get copied over by fork() and the like...
	proc->p_fd_table[STDIN_FILENO] = fh_stdin;
	proc->p_fd_table[STDOUT_FILENO] = fh_stdout;
	proc->p_fd_table[STDERR_FILENO] = fh_stderr;

	return result;
}

/* Check if a file descriptor is valid*/
static
int
check_valid_fd(int fd)
{
	if((fd < 0) || (fd > OPEN_MAX-1))
	{
		//kprintf("Failed check fd with fd = %d.\n", fd);
		return EBADF;
	}
	//kprintf("Passed check fd with fd = %d.\n", fd);
	return 0;	
}

/* Check if a file descriptor is open */
static
int
check_open_fd(int fd, struct process* proc)
{
	if(proc->p_fd_table[fd] == NULL)
	{
		return EBADF;
	}
	return 0;
}

/* Check if a  user-supplied pointer is valid.
 * We do this by dereferencing the pointer and copying
 * in a single byte. We can do this because we're 
 * protected by the copyin logic
 */
 static
 int
 check_userptr(const_userptr_t ptr)
 {
 	char kbuf;
 	int err = copyin(ptr, &kbuf,1);
 	if(err)
 	{
 		return err;
 	}
 	return 0;
 }

/* Passes badcall_c now*/
int
sys_open(char* filename, int flags, int *retval)
{

	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	struct file_handle *fh;
	struct file_object *fo;
	int fd;
	int fo_index = -1; 	// Index into file object array if it exits; -1 if no object and need to create.
	int fo_free = -1; 	// Next free index in file object list.
	int result = 0;
	char path[PATH_MAX];		// Passed to vfs_open, since it maybe destroyed
	


	//If filename is null, return
	if(filename == NULL)
	{
		return EFAULT;
	}
	//Check if filename is a valid pointer by attempting to dereference it and copy a single byte.
	result = check_userptr((const_userptr_t) filename);
	if(result)
	{
		return result;
	}
	//File name pointer is valid. Now, check if it's empty:
	if(strlen(filename) == 0)
	{
		return EINVAL;
	}
	//if(filename[sizeof(filename)] == ':') {
	//	kprintf("Writing null term to %s\n", filename);
	//	filename[sizeof(filename)] = '\0';
	//}

	//Check for invalid flags. Crude, but it works...
	//Make sure we have one of O_RDONLY, O_WRONLY, O_RDWR
	if( (O_ACCMODE & flags) > 2)
	{
		return EINVAL;
	}
	//Now, ensure other flags are present (and no more)
	if( !( flags < (O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_RDONLY | O_WRONLY | O_RDWR) ))
	{
		return EINVAL;
	}

	// Find avaiable file descritpor.
	fd = get_free_file_descriptor(proc->p_id);
	if(fd < 0) {
		// fd table is full; no fd's available to allocate.		
		return EMFILE;
	}

	// vfs_open could destroy filename, so hand it a copy.
	strcpy(path, filename);

	// Create file handle, with flags initialized; place pointer in process fd table.
	fh = fh_create(path, flags);
	if(fh == NULL) {
		//kprintf("Failed to create file handle.\n");
		return EMFILE;
	}
	proc->p_fd_table[fd] = fh;

	// Check if file object exists already.
	lock_acquire(fo_list_lock);
	fo_index = check_file_object_list(path, &fo_free);
	if((fo_index < 0) && (O_CREAT & flags)) {
		// If it doesnt exist, create a new file object.
		fo = fo_create(path);
		kprintf("created new file\n");
		file_object_list[fo_free] = fo;
			// Open/create vnode, which is the file. 
		//lock_acquire(fo->fo_vnode_lk);
		//result = vfs_open(path, flags,/* Ignore MODE*/ 0, &(fo->fo_vnode));
		//lock_release(fo->fo_vnode_lk);
	}
	else if((fo_index < 0) && !(O_CREAT & flags)) {
		// No file object and create wasnt flagged, so exit.
		lock_release(fo_list_lock);
		return ENOENT;
	}
	else if((fo_index >= 0) && ((O_CREAT|O_EXCL) & flags)) {
		// Create and excl selected, but file exists so exit.
		lock_release(fo_list_lock);
		return EEXIST;
	}
	else {
		// If it exits, link to it in file handle.
		//kprintf("Linking to file %s\n", file_object_list[fo_index]->fo_name);
		fo = file_object_list[fo_index];
	}
	lock_release(fo_list_lock);

	if(fo == NULL) {
		return ENFILE;
	}
	fh->fh_file_object = fo;

	// Open/create vnode, which is the file. 
	lock_acquire(fo->fo_vnode_lk);
		KASSERT(lock_do_i_hold(fo->fo_vnode_lk));
		//for(int i = 0; i < 100000; i++) {(void)i;}
		result = vfs_open(path, flags,/* Ignore MODE*/ 0, &(fo->fo_vnode));
	lock_release(fo->fo_vnode_lk);
	if(result)
	{
		return EIO;
	}
	//kprintf("vn op %x\n", (unsigned int)*fo);

	*retval = fd;
	// Successful sys_open, return file descriptor.
	return 0;
}

int
sys_write(int fd, const void* buf, size_t nbytes, int* retval)
{
	struct iovec iov;
	struct uio u;
	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);

	//Check if fd is outside valid range (part one :) )
	if(fd < 0 || fd >= FD_MAX)
	{
		return EBADF;
	}
	//Check if buffer is NULL:
	if(buf == NULL)
	{
		return EFAULT;
	}
	//Check buffer is a valid pointer by attempting to dereference it and copy a single byte.
	int result = check_userptr( (const_userptr_t) buf);
	if(result)
	{
		return result;
	}
	//kprintf("Current write buff: %s", (char*)buf);
	// Check if the fd has a file handle attached to it.
	result = check_open_fd(fd,proc);
	if(result)
	{
		return result;
	}
	// fd seems legit, now start checking the file handle
	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	// Check that file flags allow for writing.
	if((fh->fh_flags & O_ACCMODE) == 0) {
		return EBADF;
	}

	struct file_object *fo = fh->fh_file_object;

	//TODO handle when fd is an actual file...
	
	if(fh->fh_flags & O_APPEND) {
		// Append mode; jump to end of file.
		kprintf("Append mode\n");
		struct stat file_stat;
		lock_acquire(fo->fo_vnode_lk);
			KASSERT(lock_do_i_hold(fo->fo_vnode_lk));
			result = VOP_STAT(fo->fo_vnode, &file_stat);
		lock_release(fo->fo_vnode_lk);
		//kprintf("result: %d\n",result);
		if(result){
			
			return result;
		}
		
		//kprintf("Passed file stat\n");
		lock_acquire(fh->fh_open_lk);
		fh->fh_offset = file_stat.st_size;
		lock_release(fh->fh_open_lk);
	}
	else if(fh->fh_flags & O_TRUNC) {
		// Truncate; offset returned to zero on each write.
		//kprintf("Truncate mode\n");
		//fh->fh_offset = 0;
	}
	else {

	}
	
	lock_acquire(fh->fh_open_lk);
	//Create an iovec struct.
	iov.iov_ubase = (userptr_t) buf; 	//User pointer is the buffer
	iov.iov_len = nbytes; 				//The lengeth is the number of bytes passed in
	//Create a uio struct, too.
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; 					//Only 1 iovec (the one we created above)
	u.uio_offset = fh->fh_offset; 		//Start at the offset
	u.uio_resid = nbytes; 				//Amount of data remaining to transfer (all of it)
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE; 				//Operation Type: write 
	u.uio_space = curthread->t_addrspace; //Get address space from curthread (is this right?)
	
	//Pass this stuff to VOP_WRITE
	lock_acquire(fo->fo_vnode_lk);
		KASSERT(lock_do_i_hold(fo->fo_vnode_lk));
		result = VOP_WRITE(fo->fo_vnode, &u);
	lock_release(fo->fo_vnode_lk);
	if(result)
	{
		lock_release(fh->fh_open_lk);
		//TODO check for nonzero error codes and return something else if needed...
		return result;
	}

	// Update file handle offset
	fh->fh_offset = u.uio_offset;
	lock_release(fh->fh_open_lk);

	//Our write succeeded. Per the man pages, return the # of bytes written (might not be everything)
	*retval = nbytes - u.uio_resid;
	return 0;
}


int
sys_read(int fd, const void* buf, size_t buflen, int* retval)
{

	int result = check_valid_fd(fd);
	if(result)
	{
		return result;
	}
	//Check if buffer is NULL:
	if(buf == NULL)
	{
		return EFAULT;
	}
	//Check buffer is a valid pointer by attempting to dereference it and copy a single byte.
	result = check_userptr( (const_userptr_t) buf);
	if(result)
	{
		return result;
	}
	//Variables and structs
	struct iovec iov;
	struct uio u;
	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	//Make sure fd refers to an open file (i.e. it's valid to read)
	result = check_open_fd(fd,proc);
	if(result)
	{
		return result;
	}

	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	struct file_object *fo = fh->fh_file_object;

	//First check that the specified file is open for reading.

	if(!(fh->fh_flags & (O_RDWR|O_RDONLY)))
	{
		if(fh->fh_flags != O_RDONLY)
		{
			return EBADF;
		}
	}

	// Check that buffer pointer is not NULL.
	if(buf == NULL)
	{
		//kprintf("Buffer pointer is bad.\n");
		return EBADF;
	}

	
	// Initialize iov and uio
	iov.iov_ubase = (userptr_t) buf;		//User Data(copied into kernel) pointer is the buffer
	iov.iov_len = buflen; 					//The lengeth is the number of bytes passed in
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; 						//Only 1 iovec
	lock_acquire(fh->fh_open_lk);
	u.uio_offset = fh->fh_offset; 			//Start at the file handle offset
	lock_release(fh->fh_open_lk);
	u.uio_resid = buflen;					//Amount of data remaining to transfer
	u.uio_segflg = UIO_USERSPACE;			//
	u.uio_rw = UIO_READ; 					//Operation Type: read 
	u.uio_space = curthread->t_addrspace;	//Get address space from curthread (is this right?)
	//kprintf("About to read %x before lk\n", (unsigned int)fo->fo_vnode);
	lock_acquire(fo->fo_vnode_lk);
		//kprintf("About to read %x after lk\n", (unsigned int)fo->fo_vnode);
		KASSERT(lock_do_i_hold(fo->fo_vnode_lk));
		result = VOP_READ(fo->fo_vnode, &u);
	lock_release(fo->fo_vnode_lk);
	if (result) {
		//kprintf("\nRead opreation failed.\n");
		lock_release(fh->fh_open_lk);
		return result;
		// return EIO;1
	}
	
	// Determine bytes read.
	//bytes_read = u.uio_offset - fh->fh_offset;
	// Update file offset in file handle.
	lock_acquire(fh->fh_open_lk);
	fh->fh_offset = u.uio_offset;
	lock_release(fh->fh_open_lk);

	*retval = buflen - u.uio_resid;

	return 0;
}

int
sys_close(int fd)
{

	//Check if fd is valid (part one :) )
	int result = check_valid_fd(fd);
	if(result)
	{
		return result;
	}

	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	// Make sure fd refers to an open file (i.e. it's valid to close)
	result = check_open_fd(fd,proc);
	if(result)
	{
		return result;
	}
	struct file_object *fo = fh->fh_file_object;

	// Decrement file handle reference count; if zero, destroy file handle.
	lock_acquire(fh->fh_open_lk);
		//kprintf("fhc was %d\n",fh->fh_open_count);
		fh->fh_open_count = (fh->fh_open_count) - 1;
		//kprintf("fhc now %d\n",fh->fh_open_count);
	lock_release(fh->fh_open_lk);

	// Only call vfs_close if the file handle count is zero. This is because vfs_open
	// creates a unique fh on each open() call. Only on a fh destroy do we call vfs_close.
	// This is very important for situations where dup2 are used, since it will have close()
	// called shortly after dup2(), but the original fh remains for the newfd, and vfs_close()
	// would cause an additional close without a matching open.
	if(fh->fh_open_count == 0) {
		lock_acquire(fo->fo_vnode_lk);
			KASSERT(lock_do_i_hold(fo->fo_vnode_lk));
			vfs_close(fo->fo_vnode);
		lock_release(fo->fo_vnode_lk);
		fh_destroy(fh);
	}

	release_file_descriptor(proc->p_id, fd);

	return 0;
}

int
sys_lseek(int fd, off_t pos, int whence, int64_t* retval64)
{
	int64_t newpos;

	//Check if fd is valid (part one :) )
	if(fd < 0 || fd >= FD_MAX)
	{
		return EBADF;
	}

	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	if(proc->p_fd_table[fd] == NULL) {
		//kprintf("File descriptor is not valid for seeking.\n");
		return EBADF;
	}
	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	//fd might be positive, but it could still be bad. Check here:
	if(fh == NULL)
	{
		return EBADF;
	}
	struct file_object *fo = fh->fh_file_object;
	/* If vnode fs is null, most likely vnode refers to device*, per vnode.h*/
	if(fo->fo_vnode->vn_fs == NULL)
	{
		return ESPIPE;
	}
	// Get the file stats so we can determine file size if needed.
	struct stat file_stat;
	int result;

	lock_acquire(fh->fh_open_lk);
	switch (whence) {
		case SEEK_SET:
			newpos = pos;
			result = 0;
			break;
		case SEEK_CUR:
			newpos = fh->fh_offset + pos;
			result = 0;
			break;
		case SEEK_END:
			lock_acquire(fo->fo_vnode_lk);
				KASSERT(lock_do_i_hold(fo->fo_vnode_lk));
				result = VOP_STAT(fo->fo_vnode, &file_stat);
			lock_release(fo->fo_vnode_lk);
			newpos = file_stat.st_size + pos;
			result = 0;
			break;
		default:
			lock_release(fh->fh_open_lk);
			return EINVAL;
			break;
	}
	
	if(newpos < 0) {
		// Offset cannot be negative
		lock_release(fh->fh_open_lk);
		return EINVAL;
	}
	else {
		fh->fh_offset = newpos;
	}
	lock_release(fh->fh_open_lk);

	*retval64 = (int)fh->fh_offset;

	return 0;
}

int
sys_dup2(int oldfd, int newfd, int* retval)
{
	
	int result = 0;

	// Dup'ing onto same fd does nothing.
	if(oldfd == newfd) {
		*retval = newfd;
		return 0;
	}
	
	result = check_valid_fd(oldfd);
	if(result)
	{
		return result;
	}

	result = check_valid_fd(newfd);
	if(result)
	{
		return result;
	}

	struct process *proc = get_process(curthread->t_pid);

	result = check_open_fd(oldfd, proc);
	if(result)
	{
		return result;
	}

	struct file_handle *fh_new = proc->p_fd_table[newfd];
	
	
	if(fh_new != NULL) {
		struct file_object *fo_new = fh_new->fh_file_object;
		// Close the open file handle and descriptor.
		lock_acquire(fo_new->fo_vnode_lk);
			KASSERT(lock_do_i_hold(fo_new->fo_vnode_lk));
			vfs_close(fo_new->fo_vnode);
		lock_release(fo_new->fo_vnode_lk);

		// Decrement file handle reference count; if zero, destroy file handle.
		lock_acquire(fh_new->fh_open_lk);
			fh_new->fh_open_count = (fh_new->fh_open_count) + 1;
		lock_release(fh_new->fh_open_lk);
		// Destroy the fh if no one has it open anymore.
		if(fh_new->fh_open_count == 0) {
			fh_destroy(fh_new);
		}

		proc->p_fd_table[newfd] = NULL;
	}
	
	// Do the dupping!
	proc->p_fd_table[newfd] = proc->p_fd_table[oldfd];
	
	// Make sure to increment open count, or fh may be destroyed on oldfd close.
	lock_acquire(proc->p_fd_table[newfd]->fh_open_lk);
		proc->p_fd_table[newfd]->fh_open_count = (proc->p_fd_table[newfd]->fh_open_count) + 1;
	lock_release(proc->p_fd_table[newfd]->fh_open_lk);

	*retval = newfd;
	return 0;
}

int
sys_chdir(const char* pathname, int* retval)
{
	int result;
	char *cd = (char*)pathname;

	//Check if pathname is a valid pointer by attempting to dereference it and copy a single byte.
	result = check_userptr( (const_userptr_t) pathname);
	if(result)
	{
		return result;
	}

	if(pathname == NULL) {
		// Pathname is an invalid pointer.
		kprintf("EFAULT in chdir");
		return EFAULT;
	}
	result = vfs_chdir(cd);
	if(result) {
		// Hard I/O error.
		return EIO;
	}

	*retval = 0;
	return 0;

}

int
sys___getcwd(char* buf, size_t buflen, int* retval)
{
	int result;
	//Check if pathname is a valid pointer by attempting to dereference it and copy a single byte.
	result = check_userptr( (const_userptr_t) buf);
	if(result)
	{
		return result;
	}
	//Check if buffer is NULL:
	if(buf == NULL)
	{
		return EFAULT;
	}

	struct iovec iov;
	struct uio u;
	// Initialize iov and uio
	iov.iov_ubase = (userptr_t) buf;		//User pointer is the buffer
	iov.iov_len = buflen; 					//The lengeth is the number of bytes passed in
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; 						//Only 1 iovec
	u.uio_offset = 0; 						//Start at the file handle offset
	u.uio_resid = buflen;					//Amount of data remaining to transfer
	u.uio_segflg = UIO_USERSPACE;			//User pointer
	u.uio_rw = UIO_READ; 					//Operation Type: read 
	u.uio_space = curthread->t_addrspace;	//Get address space from curthread (is this right?)
	
	// Change the current working directory.
	result = vfs_getcwd(&u);
	if (result) {
		return EIO;
	}

	*retval = buflen - u.uio_resid;
	return 0;
}

int
sys_remove(const char* filename, int* retval)
{
	// Simple remove syscall, not fully debugged.

	int fo_free;	// Unused index refenece.
	int fo_index;	// Refence to fo in fo_list.
	char* file = (char*)filename;

	// Check if file object exists already.
	lock_acquire(fo_list_lock);
		fo_index = check_file_object_list(file, &fo_free);
	
	// Remove file from list, but vnode not deleted until list fh reference is closed.
		if(fo_index >= 0) {
				file_object_list[fo_index] = NULL;
		}
	lock_release(fo_list_lock);

	*retval = 0;

	return 0;
}

int
sys_getpid(int* retval)
{
	//Sanity Check
	KASSERT(curthread != NULL);
	*retval = (int) curthread->t_pid;
	return 0;
}

void
sys_exit(int exitcode)
{
	// kprintf("Exit%d\n",curthread->t_pid
	int encoded = _MKWAIT_EXIT(exitcode);
	process_exit(curthread->t_pid,encoded);
	return;
}

int
sys_waitpid(pid_t pid, int* status, int options, int* retval)
{
	pid_t curpid = curthread->t_pid; /*getpid()*/
	//We don't support any "options"
	if(options != 0)
	{
		return EINVAL;
	}
	//Check if status is null
	if(status == NULL)
	{
		return EFAULT;
	}
	//Check if status is an invalid pointer. If so, return err from copyin.
	int kstatus;// = status;
	int err = copyin((const_userptr_t) status, &kstatus,sizeof(int));
	if(err)
	{
		return err;
	}
	//If the process doesn't exist or pid is invalid:
	pidstate_t pidstate = get_pid_state(pid);
	if(pidstate == P_FREE || pidstate == P_INVALID)
	{
		return ESRCH;
	}
	//If we are not the parent
	if(get_process_parent(pid) != curpid)
	{
		return ECHILD;
	}
	kstatus = process_wait(curpid, pid);
	copyout(&kstatus, (userptr_t) status, sizeof(int));
	*retval = pid;
	return 0;
}

int
sys_fork(struct trapframe *tf, int* retval)
{
	// int x = splhigh();
	//Make a copy of the trapframe, so we can pass it to the child:
	struct trapframe *frame = kmalloc(sizeof(struct trapframe));
	if(frame == NULL)
	{
		//If frame is null, we're out of memory
		return ENOMEM;
	}
	memcpy(frame,tf,sizeof(struct trapframe));
	
	//Get the current process, so we can wait until fork is done.
	struct process *cur = get_process(curthread->t_pid);

	//Create the new process
	unsigned long curpid = (unsigned long) curthread->t_pid;
	struct process *newprocess = NULL;
	// splhigh();
	int result = thread_forkf(cur->p_name, enter_forked_process,(void*) frame,curpid,NULL,&newprocess);
	if(result)
	{
		kfree(frame);
		return result;
	}	
	//Wait until the child starts to return from fork.
	

	*retval = newprocess->p_id;
	// V(cur->p_forksem);

	// spl0();
	// splx(x);
	return 0;
}

/*
 * waitpid() system call, for use by the kernel ONLY.
 * This should be called !!!ONLY!!! from the kernel menu, as it does not check to see
 * if the status pointer passed in is safe or not.
 */
int
kern_sys_waitpid(pid_t pid, int* status, int options, int* retval)
{
	kprintf("about to wait on %d\n", pid);
	pid_t curpid = curthread->t_pid; /*getpid()*/
	//We don't support any "options"
	if(options != 0)
	{
		return EINVAL;
	}
	//Check if status is null
	if(status == NULL)
	{
		return EFAULT;
	}
	//If the process doesn't exist or pid is invalid:
	pidstate_t pidstate = get_pid_state(pid);
	if(pidstate == P_FREE || pidstate == P_INVALID)
	{
		return ESRCH;
	}
	//If we are not the parent
	if(get_process_parent(pid) != curpid)
	{
		return ECHILD;
	}
	*status = process_wait(curpid, pid);
	*retval = pid;
	return 0;
}

/* The exec() system call */
/* Passes badcall_a (bad execv) */
/* AKA don't touch me :) */
int
sys_execv(const char* program, char** args, int* retval)
{
	char kprogram[128]; (void) kprogram;
	size_t bytesCopied = 0;
	int result = 0;
	size_t actual = 0;

	if(args ==  NULL)
	{
		//Args was NULL, return EFAULT
		return EFAULT;	
	}

	result = copyinstr((const_userptr_t) program, (void*) kprogram,128,&actual);
	if(result)
	{
		//program pointer was invalid, return error
		return result;
	}
	if(actual <= 1)
	{
		//An empty program string was passed into execv, return EINVAL
		return EINVAL;
	}
	result = check_userptr( (const_userptr_t) args);
	if(result)
	{
		return result;
	}
	// char kargs;
	// // const_userptr_t uargs = (void*) args;
	// result = copyin( (const_userptr_t) args, &kargs, 1);
	// if(result)
	// {
	// 	//args pointer was invalid, return error
	// 	return result;
	// }

	// do {
	// 	result = copyin((userptr_t) program,&curchar,sizeof(char));
	// 	if(result)
	// 	{
	// 		return result;
	// 	}
	// 	kprogram[bytesCopied] = curchar;
	// 	bytesCopied++;
	// 	program = program+1; 

	// } while(curchar != '\0');

	// kprintf("Copied in program\n");
	size_t count = 0;
	bytesCopied = 0;

	size_t max = 1024 * 4;
	(void)max;
	// userptr_t curusrarg;
	char* curarg;
	// int* x;
	(void)curarg;

	char* buf = (char*) kmalloc(max);
	char** argptrs = kmalloc(max);
	actual = 0;
	void* curcharptr;
	// size_t curptrpos = max-4;
	while(args[count] != NULL)
	{
		curcharptr = &buf[bytesCopied];
		// kprintf("Actual: %d Pos: %d\n",actual,bytesCopied);
		result = copyinstr((const_userptr_t) args[count], (void*) &buf[bytesCopied], max-actual,&actual);
		if(result)
		{	
			// kprintf("copy failed!");
			return result;
		}
		else { /*kprintf("%d bytes copied\n", actual);*/ bytesCopied += actual;}
		while((max - bytesCopied) % 4 != 0)
		{
			// kprintf("padding args[%d]",actual);
			buf[bytesCopied] = '\0';
			bytesCopied++;
		}
		//Add arg ptr to arg array:
		argptrs[count] = curcharptr;
		count++;
	}
	// argptrs[count+1] = NULL;
	// while(args[count] != NULL)
	// {
	// 	//Copy in pointer to current arg..
	// 	curusrarg = (userptr_t) args[count];
	// 	// result = copyin((const_userptr_t) args[count], (void*) curarg, 1);
	// 	result = copyinstr((const_userptr_t) arg[count], curarg, 4)
	// 	if(result)
	// 	{
	// 		kprintf("copy failed!\n");
	// 		return result;
	// 	}
	// 	kprintf("copy worked!");
	// 	//Copy in the arg, char by char.
	// 	do
	// 	{	
	// 		if(bytesCopied == max)
	// 		{
	// 			return E2BIG;
	// 		}
	// 		result = copyin(curusrarg, &curchar, sizeof(char));
	// 		if(result)
	// 		{
	// 			return result;
	// 		}
	// 		buf[bytesCopied] = curchar;
	// 		bytesCopied++;
	// 		curusrarg++;
	// 	} while(curchar != '\0');
	// 	//pad the arg, if needed
	// 	while(bytesCopied % 4 != 0)
	// 	{
	// 		buf[bytesCopied] = '\0';
	// 		bytesCopied++;
	// 	}
	// 	count++;

	// }

	// for(int i=0;i<bytesCopied;i++)
	// {	
	// 	kprintf("%s\n", (char *) &buf[i]);
	// }

	// char* kargs[count+1];
	// int pos = 0;
	// kargs[count] = NULL;
	// for(size_t j=0;j<count;j++)
	// {
	// 	kargs[j] = (char*) &buf[pos];
	// 	while(buf[pos] != '\0')
	// 	{
	// 		pos++;
	// 	}
	// }
	// kprintf("kargs[0]:%s\n", (char*) kargs[0]);
	// kprintf("kargs[1]:%s\n", (char*) kargs[1]);
	// kfree(buf);
	// for(size_t i = 0;i<bytesCopied;i++)
	// {
	// 	kprintf("buf[%d]: %c\n",i, (char) buf[i]);
	// }
	// for(size_t i = 0;i<count;i++)
	// {
	// 	kprintf("ptrs[%d]: %s\n",i, argptrs[i]);
	// }
	runprogram2(&kprogram[0], argptrs,count);
	(void)program;
	(void)args;
	(void)retval;
	return 0;
}

