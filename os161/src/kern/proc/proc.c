

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <kern/errno.h>
#include <thread.h>
#include <synch.h>
#include <filedesc.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;
/*
 * Create a proc structure.
 */
struct lock 		*lk_allproc;

int			next_pid;
static
void
proc_add_to_allproc( struct proc *p, int spot ) {
	lock_acquire( lk_allproc );
	p_table[spot] = p;
	lock_release( lk_allproc );
}


static
void
proc_found_spot( pid_t *pid, int spot ) {
	//if we are being called, allproc[spot] must be null.
	KASSERT( p_table[spot] == NULL );
	
	*pid = spot;
			
	//adjust next_pid to be the one just given.
	next_pid = spot + 1;

	//release the lock
	lock_release( lk_allproc );
}
static
int
proc_alloc_pid( pid_t *pid ) {
	int		i = 0;
	lock_acquire( lk_allproc );
	if( next_pid >= MAX_PROCESSES )
		next_pid = 0;
	for( i = next_pid; i < MAX_PROCESSES; ++i ) {
		if( p_table[i] == NULL ) {
			proc_found_spot( pid, i );
			return 0;
		}
	}
	for( i = 0; i < next_pid; ++i ) {
		if( p_table[i] == NULL ) {
			proc_found_spot( pid, i );
			return 0;
		}
	}
	lock_release( lk_allproc );
	return ENPROC;
}

static
void
proc_dealloc_pid( pid_t pid ) {
	lock_acquire( lk_allproc );
	KASSERT( p_table[pid] != NULL );
	p_table[pid] = NULL;
	lock_release( lk_allproc );
}


int
proc_create( struct proc **res )  {
	struct proc	*p = NULL;
	int		err = 0;
	pid_t		pid;
	err = proc_alloc_pid( &pid );
	if( err )
		return err;
	p = kmalloc( sizeof( struct proc ) );
	if( p == NULL ) {
		proc_dealloc_pid( pid );
		return ENOMEM;
	}
	p->p_pid = pid;
	err = fd_create(& p->p_fd );
	if( err ) {
		kfree( p );
		proc_dealloc_pid( pid );
		return err;
	}
	p->lock = lock_create( "lock" );
	if( p->lock == NULL ) {
		fd_destroy( p->p_fd );
		kfree( p );
		proc_dealloc_pid( pid );
		return ENOMEM;
	}
	p->p_sem = sem_create( "p_sem", 0 );
	if( p->p_sem == NULL ) {
		lock_destroy( p->lock );
		fd_destroy( p->p_fd  );
		kfree( p );
		proc_dealloc_pid( pid );
		return ENOMEM;
	}
	p->p_retval = 0;
	p->p_is_dead = false;
	p->p_nsyscalls = 0;
	p->p_nice = 0;
	p->exitcode = 0;
	p->p_proc = NULL;
	proc_add_to_allproc( p, pid );
	*res = p;
	return 0;
}

int
proc_copy( struct proc *source, struct proc **target ) {
	struct proc		*p = NULL;
	int			err;
	err = proc_create( &p );
	if( err )
		return err;
	//source=curthread->td_proc;
	file_copy( source->p_fd, p->p_fd);
	*target = p;	
	return 0;	
}
void proc_destroy(struct proc *proc)
{
	pid_t		pid;

	//copy the pid for later used.
	pid = proc->p_pid;
	
	//destroy the cv
	sem_destroy( proc->p_sem );

	//destroy the lock associated with it.
	lock_destroy( proc->lock );

	//destroy the filedescriptor table
	fd_destroy( proc->p_fd );

	//free the memory.
	kfree( proc );

	//deallocate the pid.
	proc_dealloc_pid( pid );
}
void 
proc_system_init( void ) {
	int 		i = 0;
	for( i = 0; i < MAX_PROCESSES; ++i ) {
		p_table[i] = NULL;
	}
	lk_allproc = lock_create( "lk_allproc" );
	if( lk_allproc == NULL ) 
		panic( "could not initialize proc system." );
	lk_exec = lock_create( "lk_exec" );
	if( lk_exec == NULL ) {
		lock_destroy( lk_allproc );
		panic( "could not create lk_exec." );
	}
	next_pid = 0;
}


int
proc_get( pid_t pid, struct proc **res ) {
	//invalid pid.
	if( pid >= MAX_PROCESSES || pid <= 0 )
		return EINVAL;

	//lock allproc.
	lock_acquire( lk_allproc );
	
	//if the requested pid is associated with a valid process
	if( p_table[pid] != NULL && p_table[pid] != (void *)PROC_RESERVED_SPOT ) {
		lock_acquire( p_table[pid]->lock );
		*res = p_table[pid];
		lock_release( lk_allproc );
		return 0;
	}
	
	//the requested pid is actually invalid.
	lock_release( lk_allproc );
	return ESRCH;
		
}
/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;
	KASSERT(proc != NULL);
	spinlock_acquire(&proc->p_lock);
	oldas = proc->t_addrspace;
	proc->t_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
int pid_alloc(pid_t * pidValue) {
	for (int i=1; i < MAX_PROCESSES; i++) {
		if (p_table[i] == NULL) {
			 *pidValue = i;
			 return 0;	// return 0 upon success
		}	
	}
	return ENPROC;	// case of error, process table is full
}


/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	//#if 0
	//kproc = proc_create("[kernel]");
	lk_allproc = lock_create( "lk_allproc" );
	 proc_create(&kproc);
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
	//#endif
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */




/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing td_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->td_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->td_proc = NULL;
	splx(spl);
}

