#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;
	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}
	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}
	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}
	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);
	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);
	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);
	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_lock(sem->sem_wchan);
		spinlock_release(&sem->sem_lock);
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
		spinlock_acquire(&sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);
	spinlock_acquire(&sem->sem_lock);
	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);
	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;
	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}
	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}
	//HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}
	spinlock_init(&lock->lk_lock);
	lock->lk_holder = NULL;
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lk_holder == NULL);
	spinlock_cleanup(&lock->lk_lock);
	wchan_destroy(lock->lk_wchan);
	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{ 
	DEBUGASSERT(lock != NULL);
    KASSERT(!(lock_do_i_hold(lock)));
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&lock->lk_lock);
	while (lock->lk_holder != NULL) {
		wchan_lock(lock->lk_wchan);
		spinlock_release(&lock->lk_lock);		
		/* here the curthread give up its cpu, switch to sleep */
		wchan_sleep(lock->lk_wchan, &lock->lk_lock);
		spinlock_acquire(&lock->lk_lock);		
	}
	lock->lk_holder = curthread;
	spinlock_release(&lock->lk_lock);

}

void
lock_release(struct lock *lock)
{ 
	 KASSERT(lock_do_i_hold(lock));
	 
	//DEBUGASSERT(lock != NULL);
	spinlock_acquire(&lock->lk_lock);
	//KASSERT(lock->lk_holder == curthread);
	lock->lk_holder = NULL;
	wchan_wakeone(lock->lk_wchan, &lock->lk_lock);
	/* Call this (atomically) when the lock is released */
	//HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
	spinlock_release(&lock->lk_lock);

}

bool
lock_do_i_hold(struct lock *lock)
{
	bool ret;
	KASSERT(lock != NULL);
	spinlock_acquire(&lock->lk_lock);
	ret = (lock->lk_holder == curthread);
	spinlock_release(&lock->lk_lock);
	return ret;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *cv_create(const char *name)
{
	struct cv *cv;
	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}
	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	//spinlock_init(&cv->cv_wchanlock);
	return cv;
}
void cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);
	//spinlock_cleanup(&cv->cv_wchanlock);
	wchan_destroy(cv->cv_wchan);
	kfree(cv->cv_name);
	kfree(cv);
}
void cv_wait(struct cv *cv, struct lock *lock)
{
	 KASSERT(lock_do_i_hold(lock));
	wchan_lock(cv->cv_wchan);
	lock_release(lock);

	wchan_sleep(cv->cv_wchan,NULL);
	lock_acquire(lock);
}
void cv_signal(struct cv *cv, struct lock *lock)
{        KASSERT(lock_do_i_hold(lock));

        wchan_wakeone(cv->cv_wchan,NULL);
}
void cv_broadcast(struct cv *cv, struct lock *lock)
{	        KASSERT(lock_do_i_hold(lock));

        wchan_wakeall(cv->cv_wchan,NULL);
}
