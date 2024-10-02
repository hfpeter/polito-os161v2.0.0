/*
 * Copyright (c) 2013
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

#ifndef _PROC_H_
#define _PROC_H_
//#define OPEN_MAX      128
/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#define MAX_PROCESSES 32
#define PROC_RESERVED_SPOT 0xcafebabe
#define PROC_MAX_HEAP_PAGES 2048
struct addrspace;
struct thread;
struct vnode;
struct lock * waitpidlock;// = lock_create("waitpid lock");
struct cv * waitpidcv;// = cv_create("waitpid cv");
struct lock * p_tablelock;
struct lock		*lk_exec;

/*

 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */
	/* VM */
	struct addrspace *t_addrspace;	/* virtual address space */
	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

	pid_t p_pid;//process id rather than parent 
	pid_t parent_id;
	struct proc		*p_proc;	/* parent process */	
	bool exited;	// flag for exit
	int exitcode;	// exitcode sent by user
	struct lock *lock;
	struct cv *cv; 
	struct thread * thread_proc;	// pointer to the thread
	/* File Table */
	//struct file_handle *t_ft[128];//[128]???
	struct filedesc	*p_fd;
	struct semaphore * p_sem;
	int status;
	bool			 p_is_dead;	/* are we dead? */
	int			     p_retval;	/* our return code */
	uint64_t		p_nsyscalls;	/* how many system calls we called? */
	int			p_nice;		/* our nice value */		
};

/* Global array of processes */
 struct proc * p_table[MAX_PROCESSES];
/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;
/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);
/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

void 
proc_system_init( void );
int proc_create( struct proc **res ) ;
int	 proc_copy(struct proc *, struct proc ** );
/* Destroy a process. */
void proc_destroy(struct proc *proc);
int	 	proc_get( pid_t, struct proc ** );
void proc_destroy2(struct proc *proc);
/* Attach a thread to a process. Must not already have a process. */
/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);
int pid_alloc(pid_t * pidValue);
void child_forkentry(void * data1, unsigned long data2) ;

#endif /* _PROC_H_ */
