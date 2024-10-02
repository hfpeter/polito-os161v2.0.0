/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2010
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

/*
 * Core kernel-level thread system.
 */

#define THREADINLINE

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>
#include <cpu.h>
#include <spl.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <threadlist.h>
#include <threadprivate.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <addrspace.h>
#include <mainbus.h>
#include <vnode.h>
#include <file_syscall.h>
#include <vfs.h>
/* Magic number used as a guard value on kernel thread stacks. */
#define THREAD_STACK_MAGIC 0xbaadf00d
/* Master array of CPUs. */
DECLARRAY(cpu, static __UNUSED inline);
DEFARRAY(cpu, static __UNUSED inline);
static struct cpuarray allcpus;
static struct semaphore *cpu_startup_sem;
////////////////////////////////////////////////////////////
static
void
thread_checkstack_init(struct thread *thread)
{
	((uint32_t *)thread->t_stack)[0] = THREAD_STACK_MAGIC;
	((uint32_t *)thread->t_stack)[1] = THREAD_STACK_MAGIC;
	((uint32_t *)thread->t_stack)[2] = THREAD_STACK_MAGIC;
	((uint32_t *)thread->t_stack)[3] = THREAD_STACK_MAGIC;
}

static
void
thread_checkstack(struct thread *thread)
{
	if (thread->t_stack != NULL) {
		KASSERT(((uint32_t*)thread->t_stack)[0] == THREAD_STACK_MAGIC);
		KASSERT(((uint32_t*)thread->t_stack)[1] == THREAD_STACK_MAGIC);
		KASSERT(((uint32_t*)thread->t_stack)[2] == THREAD_STACK_MAGIC);
		KASSERT(((uint32_t*)thread->t_stack)[3] == THREAD_STACK_MAGIC);
	}
}
struct thread *thread_create(const char *name)
{
	struct thread *thread;
	DEBUGASSERT(name != NULL);
	thread = kmalloc(sizeof(*thread));
	if (thread == NULL) {
		return NULL;
	}
	thread->t_name = kstrdup(name);
	if (thread->t_name == NULL) {
		kfree(thread);
		return NULL;
	}
	thread->t_wchan_name = "NEW";
	thread->t_state = S_READY;
	/* Thread subsystem fields */
	thread_machdep_init(&thread->t_machdep);
	threadlistnode_init(&thread->t_listnode, thread);
	thread->t_stack = NULL;
	thread->t_context = NULL;
	thread->t_cpu = NULL;
	/* Interrupt state fields */
	thread->t_in_interrupt = false;
	thread->t_curspl = IPL_HIGH;
	thread->t_iplhigh_count = 1; /* corresponding to t_curspl */
	thread->t_vmp_count = 0;
	thread->t_clone = 0;
	/* VM fields */
	thread->t_addrspace = NULL;
	/* VFS fields */
	thread->t_cwd = NULL;
	/* If you add to struct thread, be sure to initialize here */
	return thread;
}
struct cpu *
cpu_create(unsigned hardware_number)
{
	struct cpu *c;
	int result;
	char namebuf[16];

	c = kmalloc(sizeof(*c));
	if (c == NULL) {
		panic("cpu_create: Out of memory\n");
	}

	c->c_self = c;
	c->c_hardware_number = hardware_number;

	c->c_curthread = NULL;
	threadlist_init(&c->c_zombies);
	c->c_hardclocks = 0;

	c->c_isidle = false;
	threadlist_init(&c->c_runqueue);
	spinlock_init(&c->c_runqueue_lock);

	c->c_ipi_pending = 0;
	c->c_numshootdown = 0;
	spinlock_init(&c->c_ipi_lock);

	result = cpuarray_add(&allcpus, c, &c->c_number);
	if (result != 0) {
		panic("cpu_create: array_add: %s\n", strerror(result));
	}

	snprintf(namebuf, sizeof(namebuf), "<boot #%d>", c->c_number);
	c->c_curthread = thread_create(namebuf);
	if (c->c_curthread == NULL) {
		panic("cpu_create: thread_create failed\n");
	}


	if (c->c_number == 0) {
	}
	else {
		c->c_curthread->t_stack = kmalloc(STACK_SIZE);
		if (c->c_curthread->t_stack == NULL) {
			panic("cpu_create: couldn't allocate stack");
		}
		thread_checkstack_init(c->c_curthread);
	}
	c->c_curthread->t_cpu = c;

	cpu_machdep_init(c);

	return c;
}
static
void
thread_destroy(struct thread *thread)
{
	KASSERT(thread != curthread);
	KASSERT(thread->t_state != S_RUN);
	/* VFS fields, cleaned up in thread_exit */
	KASSERT(thread->t_cwd == NULL);

	/* VM fields, cleaned up in thread_exit */
	KASSERT(thread->t_addrspace == NULL);
	/* Thread subsystem fields */

	if (thread->t_stack != NULL) {
		kfree(thread->t_stack);
	}
	threadlistnode_cleanup(&thread->t_listnode);
	thread_machdep_cleanup(&thread->t_machdep);

	/* sheer paranoia */
	thread->t_wchan_name = "DESTROYED";

	kfree(thread->t_name);
	kfree(thread);
}
static void exorcise(void)
{
	struct thread *z;
	while ((z = threadlist_remhead(&curcpu->c_zombies)) != NULL) {
		KASSERT(z != curthread);
		KASSERT(z->t_state == S_ZOMBIE);
		thread_destroy(z);
	}
}
void
thread_panic(void)
{
	ipi_broadcast(IPI_PANIC);
	curcpu->c_runqueue.tl_count = 0;
	curcpu->c_runqueue.tl_head.tln_next = NULL;
	curcpu->c_runqueue.tl_tail.tln_prev = NULL;
}
void
thread_shutdown(void)
{
	ipi_broadcast(IPI_OFFLINE);
}
void
thread_bootstrap(void)
{
	struct cpu *bootcpu;
	struct thread *bootthread;

	cpuarray_init(&allcpus);
	bootcpu = cpu_create(0);
	bootthread = bootcpu->c_curthread;
	INIT_CURCPU(bootcpu, bootthread);
	curthread->t_cpu = curcpu;
	curcpu->c_curthread = curthread;

}
void
cpu_hatch(unsigned software_number)
{
	char buf[64];

	KASSERT(curcpu != NULL);
	KASSERT(curthread != NULL);
	KASSERT(curcpu->c_number == software_number);

	spl0();
	cpu_identify(buf, sizeof(buf));

	kprintf("cpu%u: %s\n", software_number, buf);

	V(cpu_startup_sem);
	thread_exit();


}

/*
 * Start up secondary cpus. Called from boot().
 */
void
thread_start_cpus(void)
{
	char buf[64];
	unsigned i;

	cpu_identify(buf, sizeof(buf));
	kprintf("cpu0: %s\n", buf);

	cpu_startup_sem = sem_create("cpu_hatch", 0);
	mainbus_start_cpus();
	
	for (i=0; i<cpuarray_num(&allcpus) - 1; i++) {
		P(cpu_startup_sem);
	}
	sem_destroy(cpu_startup_sem);
	cpu_startup_sem = NULL;


}
static
void
thread_make_runnable(struct thread *target, bool already_have_lock)
{
	struct cpu *targetcpu;
	bool isidle;
	/* Lock the run queue of the target thread's cpu. */
	targetcpu = target->t_cpu;
	if (already_have_lock) {
		/* The target thread's cpu should be already locked. */
		KASSERT(spinlock_do_i_hold(&targetcpu->c_runqueue_lock));
	}
	else {
		spinlock_acquire(&targetcpu->c_runqueue_lock);
	}

	isidle = targetcpu->c_isidle;
	threadlist_addtail(&targetcpu->c_runqueue, target);
	if (isidle) {
		ipi_send(targetcpu, IPI_UNIDLE);
	}
	if (!already_have_lock) {
		spinlock_release(&targetcpu->c_runqueue_lock);
	}
}

#define MAX_THREADS 1024

typedef struct {
    struct thread * thread_id;
    void* stack_ptr;
    // 其他需要记录的线程信息
} thread_info;
thread_info thread_list[MAX_THREADS];
int thread_count = 0;
int thread_fork(const char *name,struct proc *proc, 
void (*entrypoint)(void *data1, unsigned long data2),
void *data1, unsigned long data2,
struct thread **ret)
{
	(void)proc;
	struct thread *newthread;

	newthread = thread_create(name);
	if (newthread == NULL) {
		return ENOMEM;
	}
	/* Allocate a stack */
	newthread->t_stack = kmalloc(STACK_SIZE);
	if (newthread->t_stack == NULL) {
		thread_destroy(newthread);
		return ENOMEM;
	}
	thread_checkstack_init(newthread);
	/* Thread subsystem fields */
	newthread->t_cpu = curthread->t_cpu;
	/* VM fields */
	/* do not clone address space -- let caller decide on that */

	/* VFS fields */
	if (curthread->t_cwd != NULL) {
		VOP_INCREF(curthread->t_cwd);
		newthread->t_cwd = curthread->t_cwd;
	}
	newthread->t_iplhigh_count++;
	switchframe_init(newthread, entrypoint, data1, data2);
	/* Lock the current cpu's run queue and make the new thread runnable */
	thread_make_runnable(newthread, false);
	if (ret != NULL) {
		*ret = newthread;
	}
	return 0;
}
static void thread_switch(threadstate_t newstate, struct wchan *wc, struct spinlock *lk)
{
	(void)lk;
	struct thread *cur, *next;
	int spl;
	DEBUGASSERT(curcpu->c_curthread == curthread);
	DEBUGASSERT(curthread->t_cpu == curcpu->c_self);
	/* Explicitly disable interrupts on this processor */
	spl = splhigh();
	cur = curthread;
	//c1
	if (curcpu->c_isidle) {
		splx(spl);
		return;
	}
	/* Check the stack guard band. */
	thread_checkstack(cur);
	/* Lock the run queue. */
	spinlock_acquire(&curcpu->c_runqueue_lock);
	/* Micro-optimization: if nothing to do, just return */
	if (newstate == S_READY && threadlist_isempty(&curcpu->c_runqueue)) {
		spinlock_release(&curcpu->c_runqueue_lock);
		splx(spl);
		return;
	}
	/* Put the thread in the right place. */
	switch (newstate) {
	    case S_RUN:
		panic("Illegal S_RUN in thread_switch\n");
	    case S_READY:
		thread_make_runnable(cur, true /*have lock*/);
		break;
	    case S_SLEEP:
		cur->t_wchan_name = wc->wc_name;
		//c2
		threadlist_addtail(&wc->wc_threads, cur);
		wchan_unlock(wc);
		break;
	    case S_ZOMBIE:
		cur->t_wchan_name = "ZOMBIE";
		threadlist_addtail(&curcpu->c_zombies, cur);
		break;
	}
	cur->t_state = newstate;
	//c3
	/* The current cpu is now idle. */
	curcpu->c_isidle = true;
	do {
		next = threadlist_remhead(&curcpu->c_runqueue);
		if (next == NULL) {
			spinlock_release(&curcpu->c_runqueue_lock);
			cpu_idle();
			spinlock_acquire(&curcpu->c_runqueue_lock);
		}
	} while (next == NULL);
	curcpu->c_isidle = false;
	//c4
	curcpu->c_curthread = next;
	curthread = next;
	/* do the switch (in assembler in switch.S) */
	switchframe_switch(&cur->t_context, &next->t_context);
	//c5
	/* Clear the wait channel and set the thread state. */
	cur->t_wchan_name = NULL;
	cur->t_state = S_RUN;
	/* Unlock the run queue. */
	spinlock_release(&curcpu->c_runqueue_lock);
	/* Activate our address space in the MMU. */
	if (cur->t_addrspace != NULL) {
		as_activate(cur->t_addrspace);
	}

	/* Clean up dead threads. */
	exorcise();
	/* Turn interrupts back on. */
	splx(spl);
}
void
thread_startup(void (*entrypoint)(void *data1, unsigned long data2),
	       void *data1, unsigned long data2)
{
	struct thread *cur;
	cur = curthread;
	/* Clear the wait channel and set the thread state. */
	cur->t_wchan_name = NULL;
	cur->t_state = S_RUN;
	/* Release the runqueue lock acquired in thread_switch. */
	spinlock_release(&curcpu->c_runqueue_lock);

	if (cur->t_addrspace != NULL) {
		as_activate(cur->t_addrspace);
	}

	/* Clean up dead threads. */
	exorcise();
	/* Enable interrupts. */
	spl0();
	/* Call the function. */
	entrypoint(data1, data2);
	/* Done. */
	thread_exit();
}
void
thread_exit(void)
{
	struct thread *cur;
	cur = curthread;
	/* VFS fields */
	if (cur->t_cwd) {
		VOP_DECREF(cur->t_cwd);
		cur->t_cwd = NULL;
	}

	/* VM fields */
	if (cur->t_addrspace) {
		struct addrspace *as = cur->t_addrspace;
		cur->t_addrspace = NULL;
		as_activate(NULL);
		as_destroy(as);
	}
	thread_checkstack(cur);
        splhigh();
	thread_switch(S_ZOMBIE, NULL, NULL);
	panic("The zombie walks!\n");
}

void fhandle_destroy(struct file_handle *fdesc) {
	KASSERT(fdesc != NULL);
	/* destroy lock and close vnode*/
	lock_destroy(fdesc->lock);
	vfs_close(fdesc->vnode);
	kfree(fdesc);
}
void
thread_yield(void)
{
	thread_switch(S_READY, NULL, NULL);
}
void schedule(void)
{
}
void
thread_consider_migration(void)
{
	unsigned my_count, total_count, one_share, to_send;
	unsigned i, numcpus;
	struct cpu *c;
	struct threadlist victims;
	struct thread *t;

	my_count = total_count = 0;
	numcpus = cpuarray_num(&allcpus);
	for (i=0; i<numcpus; i++) {
		c = cpuarray_get(&allcpus, i);
		spinlock_acquire(&c->c_runqueue_lock);
		total_count += c->c_runqueue.tl_count;
		if (c == curcpu->c_self) {
			my_count = c->c_runqueue.tl_count;
		}
		spinlock_release(&c->c_runqueue_lock);
	}

	one_share = DIVROUNDUP(total_count, numcpus);
	if (my_count < one_share) {
		return;
	}

	to_send = my_count - one_share;
	threadlist_init(&victims);
	spinlock_acquire(&curcpu->c_runqueue_lock);
	for (i=0; i<to_send; i++) {
		t = threadlist_remtail(&curcpu->c_runqueue);
		threadlist_addhead(&victims, t);
	}
	spinlock_release(&curcpu->c_runqueue_lock);

	for (i=0; i < numcpus && to_send > 0; i++) {
		c = cpuarray_get(&allcpus, i);
		if (c == curcpu->c_self) {
			continue;
		}
		spinlock_acquire(&c->c_runqueue_lock);
		while (c->c_runqueue.tl_count < one_share && to_send > 0) {
			t = threadlist_remhead(&victims);
			/*
			 * Ordinarily, curthread will not appear on
			 * the run queue. However, it can under the
			 * following circumstances:
			 *   - it went to sleep;
			 *   - the processor became idle, so it
			 *     remained curthread;
			 *   - it was reawakened, so it was put on the
			 *     run queue;
			 *   - and the processor hasn't fully unidled
			 *     yet, so all these things are still true.
			 *
			 * If the timer interrupt happens at (almost)
			 * exactly the proper moment, we can come here
			 * while things are in this state and see
			 * curthread. However, *migrating* curthread
			 * can cause bad things to happen (Exercise:
			 * Why? And what?) so shuffle it to the end of
			 * the list and decrement to_send in order to
			 * skip it. Then it goes back on our own run
			 * queue below.
			 */
			if (t == curthread) {
				threadlist_addtail(&victims, t);
				to_send--;
				continue;
			}

			t->t_cpu = c;
			threadlist_addtail(&c->c_runqueue, t);
			DEBUG(DB_THREADS,
			      "Migrated thread %s: cpu %u -> %u",
			      t->t_name, curcpu->c_number, c->c_number);
			to_send--;
			if (c->c_isidle) {
				/*
				 * Other processor is idle; send
				 * interrupt to make sure it unidles.
				 */
				ipi_send(c, IPI_UNIDLE);
			}
		}
		spinlock_release(&c->c_runqueue_lock);
	}

	/*
	 * Because the code above isn't atomic, the thread counts may have
	 * changed while we were working and we may end up with leftovers.
	 * Don't panic; just put them back on our own run queue.
	 */
	if (!threadlist_isempty(&victims)) {
		spinlock_acquire(&curcpu->c_runqueue_lock);
		while ((t = threadlist_remhead(&victims)) != NULL) {
			threadlist_addtail(&curcpu->c_runqueue, t);
		}
		spinlock_release(&curcpu->c_runqueue_lock);
	}

	KASSERT(threadlist_isempty(&victims));
	threadlist_cleanup(&victims);
}

////////////////////////////////////////////////////////////

/*
 * Wait channel functions
 */

/*
 * Create a wait channel. NAME is a symbolic string name for it.
 * This is what's displayed by ps -alx in Unix.
 *
 * NAME should generally be a string constant. If it isn't, alternate
 * arrangements should be made to free it after the wait channel is
 * destroyed.
 */
struct wchan *
wchan_create(const char *name)
{
	struct wchan *wc;

	wc = kmalloc(sizeof(*wc));
	if (wc == NULL) {
		return NULL;
	}
	spinlock_init(&wc->wc_lock);	
	threadlist_init(&wc->wc_threads);
	wc->wc_name = name;

	return wc;
}

/*
 * Destroy a wait channel. Must be empty and unlocked.
 * (The corresponding cleanup functions require this.)
 */
void
wchan_destroy(struct wchan *wc)
{
	spinlock_cleanup(&wc->wc_lock);
	threadlist_cleanup(&wc->wc_threads);
	kfree(wc);
}
/*
 * Yield the cpu to another process, and go to sleep, on the specified
 * wait channel WC, whose associated spinlock is LK. Calling wakeup on
 * the channel will make the thread runnable again. The spinlock must
 * be locked. The call to thread_switch unlocks it; we relock it
 * before returning.
 */


/*
 * Wake up one thread sleeping on a wait channel.
 */
void
wchan_wakeone(struct wchan *wc, struct spinlock *lk)
{
	struct thread *target;
	(void)lk;

	/* Grab a thread from the channel */
	spinlock_acquire(&wc->wc_lock);
	target = threadlist_remhead(&wc->wc_threads);
	spinlock_release(&wc->wc_lock);

	if (target == NULL) {
		/* Nobody was sleeping. */
		return;
	}



	thread_make_runnable(target, false);
}

/*
 * Wake up all threads sleeping on a wait channel.
 */
void
wchan_wakeall(struct wchan *wc, struct spinlock *lk)
{
	(void)lk;
	struct thread *target;
	struct threadlist list;


	threadlist_init(&list);

	/*
	 * Grab all the threads from the channel, moving them to a
	 * private list.
	 */
	spinlock_acquire(&wc->wc_lock);
	while ((target = threadlist_remhead(&wc->wc_threads)) != NULL) {
		threadlist_addtail(&list, target);
	}
	spinlock_release(&wc->wc_lock);

	/*
	 * We could conceivably sort by cpu first to cause fewer lock
	 * ops and fewer IPIs, but for now at least don't bother. Just
	 * make each thread runnable.
	 */
	while ((target = threadlist_remhead(&list)) != NULL) {
		thread_make_runnable(target, false);
	}

	threadlist_cleanup(&list);
}
void
wchan_lock(struct wchan *wc)
{
	spinlock_acquire(&wc->wc_lock);
}

void
wchan_unlock(struct wchan *wc)
{
	spinlock_release(&wc->wc_lock);
}
void wchan_sleep(struct wchan *wc, struct spinlock *lk)
{
	/* may not sleep in an interrupt handler */
	KASSERT(!curthread->t_in_interrupt);

	thread_switch(S_SLEEP, wc, lk);

}
/*
 * Return nonzero if there are no threads sleeping on the channel.
 * This is meant to be used only for diagnostic purposes.
 */
bool
wchan_isempty(struct wchan *wc, struct spinlock *lk)
{
	bool ret;
	(void)lk;
	spinlock_acquire(&wc->wc_lock);
	ret = threadlist_isempty(&wc->wc_threads);
	spinlock_release(&wc->wc_lock);

	return ret;
}

////////////////////////////////////////////////////////////

/*
 * Machine-independent IPI handling
 */

/*
 * Send an IPI (inter-processor interrupt) to the specified CPU.
 */
void
ipi_send(struct cpu *target, int code)
{
	KASSERT(code >= 0 && code < 32);

	spinlock_acquire(&target->c_ipi_lock);
	target->c_ipi_pending |= (uint32_t)1 << code;
	mainbus_send_ipi(target);
	spinlock_release(&target->c_ipi_lock);
}

/*
 * Send an IPI to all CPUs.
 */
void
ipi_broadcast(int code)
{
	unsigned i;
	struct cpu *c;

	for (i=0; i < cpuarray_num(&allcpus); i++) {
		c = cpuarray_get(&allcpus, i);
		if (c != curcpu->c_self) {
			ipi_send(c, code);
		}
	}
}

/*
 * Send a TLB shootdown IPI to the specified CPU.
 */
void
ipi_tlbshootdown(struct cpu *target, const struct tlbshootdown *mapping)
{
	unsigned n;

	spinlock_acquire(&target->c_ipi_lock);

	n = target->c_numshootdown;
	if (n == TLBSHOOTDOWN_MAX) {
		target->c_numshootdown = TLBSHOOTDOWN_ALL;
	}
	else {
		target->c_shootdown[n] = *mapping;
		target->c_numshootdown = n+1;
	}

	target->c_ipi_pending |= (uint32_t)1 << IPI_TLBSHOOTDOWN;
	mainbus_send_ipi(target);

	spinlock_release(&target->c_ipi_lock);
}

/*
 * Handle an incoming interprocessor interrupt.
 */
void
interprocessor_interrupt(void)
{	uint32_t bits;
	int i;

	spinlock_acquire(&curcpu->c_ipi_lock);
	bits = curcpu->c_ipi_pending;

	if (bits & (1U << IPI_PANIC)) {
		/* panic on another cpu - just stop dead */
		cpu_halt();
	}
	if (bits & (1U << IPI_OFFLINE)) {
		/* offline request */
		spinlock_acquire(&curcpu->c_runqueue_lock);
		if (!curcpu->c_isidle) {
			kprintf("cpu%d: offline: warning: not idle\n",
				curcpu->c_number);
		}
		spinlock_release(&curcpu->c_runqueue_lock);
		kprintf("cpu%d: offline.\n", curcpu->c_number);
		cpu_halt();
	}
	if (bits & (1U << IPI_UNIDLE)) {
		/*
		 * The cpu has already unidled itself to take the
		 * interrupt; don't need to do anything else.
		 */
	}
	if (bits & (1U << IPI_TLBSHOOTDOWN)) {
		if (curcpu->c_numshootdown == TLBSHOOTDOWN_ALL) {
			vm_tlbshootdown_all();
		}
		else {
			for (i=0; i<curcpu->c_numshootdown; i++) {
				vm_tlbshootdown(&curcpu->c_shootdown[i]);
			}
		}

		curcpu->c_numshootdown = 0;
	}

	curcpu->c_ipi_pending = 0;
	spinlock_release(&curcpu->c_ipi_lock);
}
