#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <file.h>
#include <filedesc.h>
#include <vfs.h>




void
des_file( struct file *f ) {
	//make sure we are not destroying something that is being used
	KASSERT( f->f_refcount == 0 );
	
	//close the associated vnode
	vfs_close( f->f_vnode );
	
	//release and destroy the lock
	lock_destroy( f->f_lk );

	//free the memory
	kfree( f );
}

/**
 * close the file given by the descriptor
 */
int
des_descriptor( struct proc *p, int fd ) {
	struct file 	*f = NULL;
	int 		err = 0;

	err = get_file( p, fd, &f );
	if( err )
		return err;
	
	//make sure there are programs using it
	KASSERT( f->f_refcount > 0 );

	//detach from the file descriptor table
	fd_detach( p->p_fd, fd );
	
	//decrease both refcounts
	f->f_refcount--;
	VOP_DECREF( f->f_vnode );

	//destroy if we are the only ones using it
	if( f->f_refcount == 0 ) {
		lock_release(f->f_lk);
		des_file( f );
		return 0;
	}

	//unlock the file
	lock_release(f->f_lk);
	return 0;
}


/**
 * find and return the file associated with the filedescriptor
 * inside the process. it will be returned locked.
 */
int		
get_file(struct proc *p, int fd, struct file **f ) {
	if( fd >= MAX_OPEN_FILES || fd < 0 )
		return EBADF;

	FD_LOCK( p->p_fd );
	if( p->p_fd->fd_ofiles[fd] != NULL ) { 
		*f = p->p_fd->fd_ofiles[fd];
		F_LOCK( *f );
		FD_UNLOCK( p->p_fd );
		return 0;
	}
	
	FD_UNLOCK( p->p_fd );
	return EBADF;
}

/**
 * Checks whether the given file descriptor exists in the table.
 */
bool	
file_descriptor_exists( struct proc *p, int fd ) {
	bool 		exists = false;

	FD_LOCK( p->p_fd );
	if( p->p_fd->fd_ofiles[fd] != NULL )
		exists = true;
	FD_UNLOCK( p->p_fd );
	
	return exists;
}

/**
 * close all open files associated with the given process.
 */
int
close_all_f( struct proc *p ) {
	int		i = 0;
	int		err;

	FD_LOCK( p->p_fd );
	for( i = 0; i < MAX_OPEN_FILES; ++i ) {
		if( p->p_fd->fd_ofiles[i] != NULL ) {
			FD_UNLOCK( p->p_fd );
			err = des_descriptor( p, i );
			if( err )
				return -1;
			FD_LOCK( p->p_fd );
		}
	}
	FD_UNLOCK( p->p_fd );
	return 0;
}
