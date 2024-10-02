#ifndef __FILEH__
#define __FILEH__

#include <vnode.h>
#include <synch.h>
#include <proc.h>

#define MAX_FILE_NAME 32

struct proc;

struct file {
	struct vnode			*f_vnode;	/* vnode of the file */
	uint16_t			f_oflags;	/* open mode */
	uint16_t			f_refcount;	/* reference count */
	off_t				f_offset;	
	struct lock			*f_lk;		
};

int		get_file(struct proc *, int, struct file ** );
int		des_descriptor( struct proc *, int );
int		close_f( struct proc *, struct file * );
bool		file_descriptor_exists( struct proc *, int );
void		des_file( struct file * );
int		close_all_f( struct proc * );


//helper function to open() files from inside the kernel.
int		k_open( struct proc *, char *, int, int *);

#define F_LOCK(x) (lock_acquire((x)->f_lk))


#endif
