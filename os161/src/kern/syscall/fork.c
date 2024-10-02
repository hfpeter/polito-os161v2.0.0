
#include <types.h> /* for various types to be accessed */
#include <thread.h> /* for the thread_fork function */
#include <current.h> /* for the value of curthread */
#include <kern/fork.h> /* to include the fork header file */
#include <addrspace.h> /* to get the as_copy, as_activate functions */
#include <lib.h> /* for the memcpy function */
#include <kern/errno.h> /* for error values */
#include <proc.h>
#include <thread.h>
#include <clock.h>
#include <copyinout.h>
#include <vfs.h>
#include <syscall.h>
#include <vnode.h>
#include <uio.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <endian.h>
#include <stat.h>
#include <vm.h>
#include <test.h>
#include <file_syscall.h>
#include <spl.h>
#include <filedesc.h>
static char		karg[64 * 1024];
static unsigned char	kargbuf[64 * 1024];

#define MAX_PROG_NAME 32
int
___waitpid( int pid, int *retval, int options ) {
	struct proc		*p = NULL;
	int			err;
	KASSERT( curthread != NULL );
	KASSERT( curthread->td_proc != NULL );
	if( options != 0 && options != WNOHANG )
		return EINVAL;
	err = proc_get( pid, &p );
	if( err )
		return err;
	if( p->p_proc != curthread->td_proc ) {
		lock_release( p->lock );
		return ECHILD;
	}
	if( !p->p_is_dead && (options == WNOHANG) ) {
		lock_release( p->lock );
		*retval = 0;
		return 0;
	}
	lock_release( p->lock );
	P( p->p_sem );
	lock_acquire( p->lock );
	KASSERT( p->p_is_dead );
	*retval = _MKWAIT_EXIT(p->p_retval);
	lock_release( p->lock );
	proc_destroy( p );
	return 0;
}

int sys_fork2(struct trapframe * tf, int * retval) ;
int
proc_copy( struct proc *source, struct proc **target ) ;
static
void
fork_child_return( void *v_args, unsigned long not_used );
struct child_fork_args {
	struct addrspace	*as_source;
	struct proc		*td_proc;
	struct trapframe	*tf;
};
#define ex1bug
#define ex2bug
//#define exbug3
//#define exbug4

static
int
trapframe_copy( struct trapframe *tf, struct trapframe **newtf ) ;


void sys_exit(int exitcode)
{
	struct proc		*p = NULL;
	int			err;
	KASSERT( curthread != NULL );
	KASSERT( curthread->td_proc != NULL );
	p = curthread->td_proc;
	//close all open files.
	err = close_all_f( p );
	if( err ) 
		panic( "error closing a file." );
	lock_acquire( p->lock );
	p->p_retval = exitcode;
	p->p_is_dead = true;
	if( p->p_proc == NULL ) {
		lock_release( p->lock );
		proc_destroy( p );
	}
	else {
		V( p->p_sem );
		lock_release( p->lock );
	}
	thread_exit();
}

static
int
trapframe_copy( struct trapframe *tf, struct trapframe **newtf ) {
	*newtf = kmalloc( sizeof( struct trapframe ) );
	if( *newtf == NULL )
		return ENOMEM;
	
	memcpy( *newtf, tf, sizeof( struct trapframe ) );
	return 0;
}
int
sys_fork( struct trapframe *tf, int *retval ) {
	struct proc		*p_new = NULL;
	struct trapframe	*tf_new = NULL;
	int			err;
	struct child_fork_args	*args;
	pid_t			pid;
	KASSERT( curthread != NULL );
	KASSERT( curthread->td_proc != NULL );
	KASSERT( tf != NULL );
	err = proc_copy( curthread->td_proc, &p_new );
	pid = p_new->p_pid;
	p_new->p_proc = curthread->td_proc;
	err = trapframe_copy( tf, &tf_new );
	if( err ) {	
		close_all_f( p_new );
		proc_destroy( p_new );
		return err;
	}
	args = kmalloc( sizeof( struct child_fork_args ) );
	if( args == NULL ) {
		kfree( tf_new );
		close_all_f( p_new );
		proc_destroy( p_new );
		return ENOMEM;
	}
	args->tf = tf_new;
	args->td_proc = p_new;
	err = as_copy( curthread->t_addrspace, &args->as_source );
	if( err ) {
		kfree( args->tf );
		kfree( args );
		close_all_f( p_new );
		proc_destroy( p_new );
		return err;
	}
	err = thread_fork(
		curthread->t_name,
		NULL,
		fork_child_return,
		args,
		0,
		NULL
	);
	if( err ) {
		as_destroy( args->as_source );
		kfree( args->tf );
		kfree( args );
		close_all_f( p_new );
		proc_destroy( p_new );
		return err;
	}
	*retval = pid;
	return 0;	
}
	

static
void
fork_child_return( void *v_args, unsigned long not_used ){
	struct child_fork_args 	*args = NULL;
	struct trapframe	tf;
	(void)not_used;
	//cast to something we can work with.
	args = v_args;
	args->tf->tf_v0 = 0;
	args->tf->tf_a3 = 0;
	args->tf->tf_epc += 4;
	curthread->td_proc = args->td_proc;
	KASSERT( curthread->t_addrspace == NULL );
	curthread->t_addrspace = args->as_source;
	as_activate( curthread->t_addrspace);//
	memcpy( &tf, args->tf, sizeof( struct trapframe ) );
	kfree( args->tf );
	kfree( args );
	mips_usermode( &tf );
}

/* entry point for the child process */
/* data1 - parent's trapframe
data2 - address space of the parent */
void child_forkentry(void * data1, unsigned long data2) {
	//(void)fork_sem;
	(void)data2;
	//P((struct semaphore *) data2);
	as_activate(curthread->t_addrspace);
	struct trapframe tf;
	tf = *(struct trapframe *)data1;
	tf.tf_a3 = 0;
	tf.tf_v0 = 0;
	tf.tf_epc += 4;
	kfree(data1);
	//V((struct semaphore *) data2);
	mips_usermode(&tf);
}
struct addrspace *as;
static
int
align_arg( char arg[64 * 1024], int align ) {
	char 	*p = arg;
	int	len = 0;
	int	diff;

	while( *p++ != '\0' )
		++len;
	
	if( ++len % align  == 0 )
		return len;

	diff = align - ( len % align );
	while( diff-- ) {
		*(++p) = '\0';
		++len;
	}

	return len;
}

/**
 * return the nearest length aligned to alignment.
 */
static
int
get_aligned_length( char arg[64 * 1024], int alignment ) {
	char *p = arg;
	int len = 0;

	while( *p++ != '\0' )
		++len;

	if( ++len % 4 == 0 )
		return len;
	
	return len + (alignment - ( len % alignment ) );
}

static
int
copy_args( userptr_t uargs, int *nargs, int *buflen ) {
	int		i = 0;
	int		err;
	int		nlast = 0;
	char		*ptr;
	unsigned char	*p_begin = NULL;
	unsigned char	*p_end = NULL;
	uint32_t	offset;
	uint32_t	last_offset;

	if( uargs == NULL )
		return EFAULT;
	*nargs = 0;
	*buflen = 0;
	i = 0;
	while( ( err = copyin( (userptr_t)uargs + i * 4, &ptr, sizeof( ptr ) ) ) == 0 ) {
		if( ptr == NULL )
			break;
		err = copyinstr( (userptr_t)ptr, karg, sizeof( karg ), NULL );
		if( err ) 
			return err;
		
		++i;
		*nargs += 1;
		*buflen += get_aligned_length( karg, 4 ) + sizeof( char * );
	}
	if( i == 0 && err )
		return err;
	*nargs += 1;
	*buflen += sizeof( char * );
	
	i = 0;
	p_begin = kargbuf;
	p_end = kargbuf + (*nargs * sizeof( char * ));
	nlast = 0;
	last_offset = *nargs * sizeof( char * );
	while( ( err = copyin( (userptr_t)uargs + i * 4, &ptr, sizeof( ptr ) ) ) == 0 ) {
		if( ptr == NULL )
			break;
		err = copyinstr( (userptr_t)ptr, karg, sizeof( karg ), NULL );
		if( err ) 
			return err;
		
		offset = last_offset + nlast;
		nlast = align_arg( karg, 4 );

		*p_begin = offset & 0xff;
		*(p_begin + 1) = (offset >> 8) & 0xff;
		*(p_begin + 2) = (offset >> 16) & 0xff;
		*(p_begin + 3) = (offset >> 24) & 0xff;
		
		memcpy( p_end, karg, nlast );
		p_end += nlast;

		p_begin += 4;

		last_offset = offset;
		++i;
	}
	
	*p_begin = 0;
	*(p_begin+1) = 0;
	*(p_begin+2) = 0;
	*(p_begin+3) = 0;
	
	return 0;
}

static
int
adjust_kargbuf( int nparams, vaddr_t stack_ptr ) {
	int 		i;
	uint32_t	new_offset = 0;
	uint32_t	old_offset = 0;
	int		index;
	for( i = 0; i < nparams-1; ++i ) {
		index = i * sizeof( char * );
		old_offset = (( 0xFF & kargbuf[index+3] ) << 24) |  (( 0xFF & kargbuf[index+2]) << 16) |
			     (( 0xFF & kargbuf[index+1]) << 8) |   (0xFF & kargbuf[index]);
		new_offset = stack_ptr + old_offset;
		memcpy( kargbuf + index, &new_offset, sizeof( int ) );
	}

	return 0;
}

int	
sys_execv( userptr_t upname, userptr_t uargs ) {
	struct addrspace		*as_new = NULL;
	struct addrspace		*as_old = NULL;
	struct vnode			*vn = NULL;
	vaddr_t				entry_ptr;
	vaddr_t				stack_ptr;
	int				err;
	char				kpname[MAX_PROG_NAME];
	int				nargs;
	int				buflen;

	KASSERT( curthread != NULL );
	KASSERT( curthread->td_proc != NULL );
	(void)uargs;
	lock_acquire( lk_exec );
	as_old = curthread->t_addrspace;
	err = copyinstr( upname, kpname, sizeof( kpname ), NULL );
	if( err ) {
		lock_release( lk_exec );
		return err;
	}
	err = vfs_open( kpname, O_RDONLY, 0, &vn );
	if( err ) {
		lock_release( lk_exec );
		return err;
	}
	err = copy_args( uargs, &nargs, &buflen );
	if( err ) {
		lock_release( lk_exec );
		vfs_close( vn );
		return err;
	}
	as_new = as_create();
	if( as_new == NULL ) {
		lock_release( lk_exec );
		vfs_close( vn );
		return ENOMEM;
	}
	as_activate( as_new );
	curthread->t_addrspace = as_new;
	err = load_elf( vn, &entry_ptr );
	if( err ) {
		curthread->t_addrspace = as_old;
		as_activate( as_old );
	
		as_destroy( as_new );
		vfs_close( vn );
		lock_release( lk_exec );
		return err;
	}
	err = as_define_stack( as_new, &stack_ptr );
	if( err ) {
		curthread->t_addrspace = as_old;
		as_activate( as_old );
		as_destroy( as_new );
		vfs_close( vn );
		lock_release( lk_exec );
		return err;
	}
	stack_ptr -= buflen;
	err = adjust_kargbuf( nargs, stack_ptr );
	if( err ) {
		curthread->t_addrspace = as_old;
		as_activate( as_old );
		as_destroy( as_new );
		vfs_close( vn );
		lock_release( lk_exec );
		return err;
	}
	err = copyout( kargbuf, (userptr_t)stack_ptr, buflen );
	if( err ) {
		curthread->t_addrspace = as_old;
		as_activate( as_old );
		as_destroy( as_new );
		vfs_close( vn );
		lock_release( lk_exec );
		return err;
	}
	lock_release( lk_exec );
	vfs_close( vn );
	as_destroy( as_old );
	enter_new_process( nargs-1, (userptr_t)stack_ptr, NULL,stack_ptr, entry_ptr );
	panic( "should not be here since it is in a new program" );
	return EINVAL;
}
