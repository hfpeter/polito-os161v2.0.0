
//#define PATH_MAX 1023
#define O_APPEND     32
//#define OPEN_MAX      128
#define O_ACCMODE     3  
#define O_RDONLY      0      /* Open for read */
#define O_WRONLY      1      /* Open for write */
#define O_RDWR        2      /* Open for read and write */
#define true 1
#define false 0
//struct __userptr { char _dummy; };
typedef struct __userptr *userptr_t;
typedef const struct __userptr *const_userptr_t;

struct file_handle {
        struct vnode *vnode;
        off_t offset;
        struct lock *lock;
        int destroy_count; /* To check if pointing file descriptors are 0 */
        int mode_open;
	bool con_file;
        //---added
        int flags;			 	// check permissions
        int refcount;		 	// for dup2    
	struct lock *file_lock;	// protect access to the fd            
};
//int ___waitpid( int pid, int *retval, int options ) ;
int sys_getcpu (int fd, int *buffer);
int sys_open(userptr_t filename, int flags, mode_t mode);
//int sys_open( userptr_t upath, int flags, int *retval ) ;
int sys_read(int fd, void *buffer, size_t nBytes);
int sys_write(int fd, void *buffer, size_t nBytes) ;
int sys_close(int fd);
int sys_lseek(int fd, off_t pos, int whence, off_t * retVal64);
int sys_dup2(int oldfd, int newfd, int *retval);
int sys_getpid(int *retval);
int sys_waitpid(pid_t childpid, userptr_t status, int options, int * retval) ;
int sys_getcwd(userptr_t buf, size_t buflen, int *retval);
int sys_chdir(userptr_t path);
int stdio_init(void) ;


