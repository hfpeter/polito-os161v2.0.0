#include <machine/trapframe.h> // for the struct trapframe
/* Flags for waitpid() and equivalent. */


/* Flags for waitpid() and equivalent. */
#define WNOHANG      1	/* Nonblocking. */
#define WUNTRACED    2	/* Report stopping as well as exiting processes. */

/* Special "pids" to wait for. */
#define WAIT_ANY     (-1)	/* Any child process. */
#define WAIT_MYPGRP  0		/* Any process in the same process group. */

/*
 * Result encoding.
 *
 * The lowest two bits say what happened; the rest encodes up to 30
 * bits of exit code. Note that the traditional Unix encoding, which
 * is different, wastes most of the bits and can only transmit 8 bits
 * of exit code...
 */
#define _WWHAT(x)  ((x)&3)	/* lower two bits say what happened */
#define _WVAL(x)   ((x)>>2)	/* the rest is the value */
#define _MKWVAL(x) ((x)<<2)	/* encode a value */

/* Four things can happen... */
#define __WEXITED    0		/* Process exited by calling _exit(). */
#define __WSIGNALED  1		/* Process received a fatal signal. */
#define __WCORED     2		/* Process dumped core on a fatal signal. */
#define __WSTOPPED   3		/* Process stopped (and didn't exit). */

/* Test macros, used by applications. */
#define WIFEXITED(x)   (_WWHAT(x)==__WEXITED)
#define WIFSIGNALED(x) (_WWHAT(x)==__WSIGNALED || _WWHAT(x)==__WCORED)
#define WIFSTOPPED(x)  (_WWHAT(x)==__WSTOPPED)
#define WEXITSTATUS(x) (_WVAL(x))
#define WTERMSIG(x)    (_WVAL(x))
#define WSTOPSIG(x)    (_WVAL(x))
#define WCOREDUMP(x)   (_WWHAT(x)==__WCORED)

/* Encoding macros, used by the kernel to generate the wait result. */
#define _MKWAIT_EXIT(x) (_MKWVAL(x)|__WEXITED)
#define _MKWAIT_SIG(x)  (_MKWVAL(x)|__WSIGNALED)
#define _MKWAIT_CORE(x) (_MKWVAL(x)|__WCORED)
#define _MKWAIT_STOP(x) (_MKWVAL(x)|__WSTOPPED)

/* sys_fork funtion prototype */
int ___waitpid( int pid, int *retval, int options ) ;
int sys_fork(struct trapframe *tf, int * retval);
void sys_exit(int exitcode);
/* child_forkentry function prototype */
void child_forkentry(void * data1, unsigned long data2);
void enter_forked_process(void *void_tf, unsigned long num);
int	sys_execv( userptr_t upname, userptr_t uargs );
//int sys_execv(const_userptr_t progname, userptr_t args);