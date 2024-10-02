#include <types.h>
#include <limits.h>
#include <file.h>
#include <filedesc.h>
#include <proc.h>
#include <current.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>
#include <synch.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <file_syscall.h>
#include <kern/unistd.h>
#include <endian.h>
#include <stat.h>
#include <lib.h>
#include <kern/wait.h>
#include <kern/fork.h> 
#include <cpu.h> 
#define SEEK_SET	0	/* Seek from beginning of file.  */
#define SEEK_CUR	1	/* Seek from current position.  */
#define SEEK_END	2	/* Seek from end of file.  */
static
bool
valid_flags( int flags ) {
	int		count = 0;
	int		accmode = flags & O_ACCMODE;

	if( accmode == O_RDWR )
		++count;
	
	if( accmode == O_RDONLY )
		++count;
	
	if( accmode == O_WRONLY )
		++count;

	return count == 1;
}
int sys_open(userptr_t filename, int flags, mode_t mode){
	char			k_filename[MAX_FILE_NAME];
	int			err;
	struct proc 		*p;
	(void)mode;
	int		retval;	
	KASSERT( curthread != NULL );
	KASSERT( curthread->td_proc != NULL );

	p = curthread->td_proc;
	if( !valid_flags( flags ) )
		return EINVAL;
	err = copyinstr( filename, k_filename, sizeof( k_filename ), NULL );
	if( err )
		return err;
	k_open( p, k_filename, flags, &retval );
	return  retval ;
}

int
k_open( struct proc *p, char *path, int flags, int *retval ) {
	struct vnode 		*vn = NULL;
	int			err;
	struct file		*f = NULL;
	struct stat 		st;
	err = vfs_open( path, flags, 0, &vn );
	if( err )
		return err;

		//create file
	struct file *res;
	res = kmalloc( sizeof( struct file ) );
	if( res == NULL )
		return ENOMEM;
	res->f_oflags = flags;
	res->f_refcount = 0;
	res->f_vnode = vn;
	res->f_offset = 0;
	res->f_lk = lock_create( "f_lk" );
	if( res->f_lk == NULL ) {
		kfree( res );
		err= ENOMEM;
	}
	f = res;
	err= 0;

	if( err ) {
		vfs_close( vn );
		return err;
	}
		if( flags & O_APPEND ) {
		err = VOP_STAT( f->f_vnode, &st );
		if( err ) {
			vfs_close( vn );
			des_file( f );
			return err;
		}
		f->f_offset = st.st_size;
	}
	F_LOCK( f );
	err = fd_attach( p->p_fd, f, retval );
	if( err ) {
		lock_release(f->f_lk);
		vfs_close( vn );
		des_file( f );
		return err;
	}
	f->f_refcount++;
	VOP_INCREF( f->f_vnode );
	lock_release(f->f_lk);
	return 0;
}

int sys_dup2(int oldfd, int newfd, int *retval) {
	struct filedesc *temp = NULL;
	bool newfdflag = false;
	if(oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX || curproc->p_fd->fd_ofiles[oldfd] == NULL) {
		return EBADF;		
	}
	if(oldfd == newfd){
		*retval = newfd;
		return 0;
	}
	if(curproc->p_fd->fd_ofiles[newfd] != NULL){
		temp = curproc->p_fd;//->fd_ofiles[newfd];
		newfdflag = true;
	}
	lock_acquire(curproc->p_fd->fd_lk);
	curproc->p_fd->fd_ofiles[newfd] = curproc->p_fd->fd_ofiles[oldfd];
	curproc->p_fd->fd_ofiles[oldfd]->f_refcount++;
	lock_release(curproc->p_fd->fd_lk);
	if(newfdflag) {
		lock_acquire(temp->fd_lk);	
		temp->fd_ofiles[oldfd]->f_refcount--;
		if(temp->fd_ofiles[oldfd]->f_refcount > 0) {
			lock_release(temp->fd_lk);
			temp = NULL;
		} else {
			lock_release(temp->fd_lk);
			lock_destroy(temp->fd_lk);
        	        vfs_close(temp->fd_ofiles[oldfd]->f_vnode);
        	        kfree(temp);
			temp = NULL;
		}
	}
	*retval = newfd;
	return 0;
}
           
int sys_read(int fd, void *buffer, size_t nBytes){
    int result;
    /* check args */
    if (fd < 0 || fd > OPEN_MAX + 1) return EBADF; // invalid fd
    if (curproc ->p_fd-> fd_ofiles[fd] == NULL) return EBADF; // invalid fd
    if (buffer == NULL) return EFAULT; 
    if (curproc->p_fd -> fd_ofiles[fd] -> f_oflags == O_WRONLY) return EACCES; // permission denied
    if (nBytes <= 0) return EINVAL; // invalid arg (nBytes should be positive)
    // enter critical section
    lock_acquire(curproc  ->p_fd -> fd_lk);
    struct uio u;
    struct iovec iov;
    uio_uinit(&iov, &u, buffer, nBytes, curproc->p_fd -> fd_ofiles[fd] -> f_offset, UIO_READ);
	   kprintf("readbuf is %s",(char *)buffer);
    size_t remaining = u.uio_resid;
    result = VOP_READ(curproc->p_fd -> fd_ofiles[fd] -> f_vnode, &u);
    if (result) {
		lock_release(curproc  ->p_fd -> fd_lk);
		return result;
	}
    remaining = nBytes - u.uio_resid;
    curproc->p_fd -> fd_ofiles[fd] -> f_offset = u.uio_offset;
    lock_release(curproc  ->p_fd -> fd_lk);
    return remaining;
}
int sys_write(int fd, void *buffer, size_t nBytes) 
{
    int result;
    if (fd < 0 || fd > OPEN_MAX + 1) return EBADF; //  
    if (curproc ->p_fd-> fd_ofiles[fd] == NULL) return EBADF; //  
    if (buffer == NULL) return EFAULT; //   
    if (curproc ->p_fd-> fd_ofiles[fd] -> f_oflags == O_RDONLY) return EACCES; //  
    if (nBytes <= 0) return EINVAL; 
    lock_acquire(curproc  ->p_fd -> fd_lk);
    struct uio u;
    struct iovec iov;
    uio_uinit(&iov, &u, buffer, nBytes, curproc->p_fd -> fd_ofiles[fd] -> f_offset, UIO_WRITE);
    size_t remaining = u.uio_resid;
    result = VOP_WRITE(curproc ->p_fd-> fd_ofiles[fd] -> f_vnode, &u);
    if (result) {
		lock_release(curproc  ->p_fd -> fd_lk);
		return result;
	}
    remaining = nBytes - u.uio_resid;
    curproc ->p_fd-> fd_ofiles[fd] -> f_offset = u.uio_offset;
    lock_release(curproc  ->p_fd -> fd_lk);
    return remaining;
}

int sys_close(int fd) 
{
    if (fd < 0 || fd > OPEN_MAX + 1) return EBADF; //  
    if (curproc ->p_fd-> fd_ofiles[fd] == NULL) return EBADF; //  
    lock_acquire(curproc  ->p_fd -> fd_lk);
    KASSERT( curproc  ->p_fd-> fd_ofiles[fd] -> f_refcount > 0); // 
    curproc  ->p_fd-> fd_ofiles[fd] -> f_refcount--;
    if (curproc  ->p_fd-> fd_ofiles[fd] -> f_refcount == 0) {
        vfs_close(curproc ->p_fd-> fd_ofiles[fd] -> f_vnode);
        lock_release(curproc ->p_fd->  fd_lk);
        lock_destroy(curproc ->p_fd-> fd_lk);
        kfree(curproc ->p_fd-> fd_ofiles[fd]);
        curproc ->p_fd-> fd_ofiles[fd] = NULL;	
        return 0;
    }
    lock_release(curproc  ->p_fd -> fd_lk);
    return 0;    
}
int	
sys_lseek( int fd, off_t offset, int whence, int64_t *retval ) {
	struct proc		*p = NULL;
	struct file		*f = NULL;
	int			err;
	struct stat		st;
	off_t			new_offset;
	KASSERT( curthread != NULL );
	KASSERT( curthread->td_proc != NULL );

	p = curthread->td_proc;
	err = get_file( p, fd, &f );
	if( err )
		return err;

	switch( whence ) {
		case SEEK_SET:
			new_offset = offset;
			break;
		
		case SEEK_CUR:
			new_offset = f->f_offset + offset;
			break;
		case SEEK_END:
			err = VOP_STAT( f->f_vnode, &st );
			if( err ) {
				lock_release(f->f_lk);
				return err;
			}
			new_offset = st.st_size + offset;
			break;
		default:
			lock_release(f->f_lk);
			return EINVAL;
	}
	err = VOP_TRYSEEK( f->f_vnode, new_offset );
	if( err ) {
		lock_release(f->f_lk);
		return err;
	}
	f->f_offset = new_offset;
	*retval = new_offset;
	lock_release(f->f_lk);
	return 0;
}	


int sys_getpid(int *retval) {
	*retval = curthread->t_pid;
	return 0;

}
int sys_getcpu (int fd, int *buffer)
{
	struct cpu * c_c;
		c_c=curthread->t_cpu;
		*buffer=(int)(c_c->c_number);		
    if (fd ==0 ) 
	return 0;
	return 0;
}
int
sys_waitpid( int pid, userptr_t uret, int options, int *retval ) {
	int			kstatus;
	int			err;
	err = ___waitpid( pid, &kstatus, options );
	if( err )
		return err;
	err = copyout( &kstatus, uret, sizeof( int ) );
	if( err )
		return err;
	
	*retval = pid;
	return 0;
}

int sys_getcwd(userptr_t buf, size_t buflen, int *retval)
{
	struct uio user;
	struct iovec iov;
	uio_kinit(&iov, &user, buf, buflen, 0, UIO_READ);
	user.uio_segflg = UIO_USERSPACE;
	user.uio_space = curproc->t_addrspace;// 
	if (buf == NULL || buf == (void *) 0x40000000 || buf == (void *) 0x80000000) {
		return EFAULT;
	}
	int err;
	if ((err = vfs_getcwd(&user) != 0))
	{
		return err;
	}
	*retval = buflen - user.uio_resid;
	return 0;
}

int sys_chdir(userptr_t path)
{
		char pathNameFromUser[255];
	size_t actual;
	int err;
	if (path == NULL || path == (void *) 0x40000000 || path == (void *) 0x80000000){
		return EFAULT;
	}
	if ((err =  copyinstr(path, pathNameFromUser, 255, &actual) != 0)){
		return err;
	}
	err = vfs_chdir(pathNameFromUser);	
	return err;
#if (0)
  struct vnode *new_dir;
	int error;
	char *fullpath;
	if(path == NULL)
		return EFAULT;
	if ((fullpath = (char *)kmalloc(__PATH_MAX)) == NULL)
		return ENOMEM;
	/* Copy path into fullpath. */
	error = copyinstr(path, fullpath, __PATH_MAX, NULL);
	if (error) {
		kfree(fullpath);
		return error;
	}
	/* Get the vnode for the new path/directory. */
	error = vfs_lookup(fullpath, &new_dir);
	if (error)
		return error;
	/* Set the new current working directory. */
	error = vfs_setcurdir(new_dir);
	if (error)
		return error;
	kfree(fullpath);
	return 0;
	#endif
}
int stdio_init() 
{
    int result;
// Ensure curproc is not NULL
if (curproc == NULL) {
    panic("curproc is NULL");
}	
// Ensure p_fd is not NULL
if (curproc->p_fd == NULL) {
    curproc->p_fd = kmalloc(sizeof(struct filedesc));
    if (curproc->p_fd == NULL) {
        panic("kmalloc for p_fd failed");
    }
    // Initialize fd_lk if necessary
    curproc->p_fd->fd_lk = lock_create("file descriptor lock");
    if (curproc->p_fd->fd_lk == NULL) {
        panic("lock_create failed for fd_lk");
    }
    // Initialize other members if necessary
    curproc->p_fd->fd_nfiles = 0;
    memset(curproc->p_fd->fd_ofiles, 0, sizeof(curproc->p_fd->fd_ofiles));
}

// Ensure fd_ofiles is properly initialized (usually part of the structure)
if (curproc->p_fd->fd_ofiles[0] == NULL) {
    curproc->p_fd->fd_ofiles[0] = kmalloc(sizeof(struct file));
    if (curproc->p_fd->fd_ofiles[0] == NULL) {
        panic("kmalloc for fd_ofiles[0] failed");
    }
}
///////////////////////////////////////////////
    char c0[] = "con:";
    curproc -> p_fd -> fd_ofiles[0] = kmalloc(sizeof(struct file));
    /*
	 * fails to malloc
	 */
    if (curproc -> p_fd -> fd_ofiles[0] == NULL) return ENOMEM;
    curproc -> p_fd -> fd_ofiles[0] -> f_oflags = O_RDONLY;
    curproc -> p_fd -> fd_ofiles[0] -> f_refcount = 1;
    curproc -> p_fd -> fd_ofiles[0] -> f_offset = 0;
    /*
	 * STDIN_FILENO 0, Standard input 
	 */
	result = vfs_open(c0, curproc -> p_fd -> fd_ofiles[0] -> f_oflags, 0664, &curproc -> p_fd -> fd_ofiles[0] -> f_vnode);
	if(result) return result;
	curproc -> p_fd -> fd_ofiles[0] -> f_lk = lock_create("std_input");
    /*
	 * fails to create lock
	 */
    if (curproc -> p_fd -> fd_ofiles[0] -> f_lk == NULL) {
        kfree(curproc -> p_fd -> fd_ofiles[0]);
        return ENOMEM;
    }
    sys_close(0); // file descriptor 0 at the start of the program can start closed
    char c1[] = "con:";
    curproc -> p_fd -> fd_ofiles[1] = kmalloc(sizeof(struct file_handle));
    /*
	 * fails to malloc
	 */
    if (curproc -> p_fd -> fd_ofiles[1] == NULL) return ENOMEM;  // no enough memory
    curproc -> p_fd -> fd_ofiles[1] -> f_oflags = O_WRONLY;
    curproc -> p_fd -> fd_ofiles[1] -> f_refcount = 1;
    curproc -> p_fd -> fd_ofiles[1] -> f_offset = 0;
    /*
	 * STDOUT_FILENO 1, Standard output 
	 */
	result = vfs_open(c1, curproc -> p_fd -> fd_ofiles[1] -> f_oflags, 0664, &curproc -> p_fd -> fd_ofiles[1] -> f_vnode);
	if(result) return result;
	curproc -> p_fd -> fd_ofiles[1] -> f_lk = lock_create("std_output");
    /*
	 * fails to create lock
	 */
    if (curproc -> p_fd -> fd_ofiles[1] -> f_lk == NULL) {
        kfree(curproc -> p_fd -> fd_ofiles[1]);
        return ENOMEM;      // no enough memory
    }
    char c2[] = "con:";
    curproc -> p_fd -> fd_ofiles[2] = kmalloc(sizeof(struct file_handle));
    /*
	 * fails to malloc
	 */
    if (curproc -> p_fd -> fd_ofiles[2] == NULL) return ENOMEM;
    curproc -> p_fd -> fd_ofiles[2] -> f_oflags = O_WRONLY;
    curproc -> p_fd -> fd_ofiles[2] -> f_refcount = 1;
    curproc -> p_fd -> fd_ofiles[2] -> f_offset = 0;
    /*
	 * STDERR_FILENO 2, Standard error  
	 */
	result = vfs_open(c2, curproc -> p_fd -> fd_ofiles[2] -> f_oflags, 0664, &curproc -> p_fd -> fd_ofiles[2] -> f_vnode);
	if(result) return result;
	curproc -> p_fd -> fd_ofiles[2] -> f_lk = lock_create("std_error");
    /*
	 * fails to create lock
	 */
    if (curproc -> p_fd -> fd_ofiles[2] -> f_lk == NULL) {
        kfree(curproc -> p_fd -> fd_ofiles[2]);
        return ENOMEM;      // no enough memory
    }
    return 0;
}
