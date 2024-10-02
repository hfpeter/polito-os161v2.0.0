
#ifndef _PROC_SYSCALL_H_
#define _PROC_SYSCALL_H_
#include <types.h>
#include <proc.h>
#include <mips/trapframe.h>
#include <synch.h>
#include <limits.h>
/* Process Table that is used to map between process structures and PIDs */
#define MAX_NO_ARGS 3851
char arguments[ARG_MAX];
int arg_pointers[MAX_NO_ARGS];
extern struct lock *arg_lock;
extern struct proc *proc_table[OPEN_MAX];
extern int proc_counter;

#endif