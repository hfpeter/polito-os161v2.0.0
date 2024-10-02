

/*
 * Basic vnode support functions.
 */
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>

/*
 * Initialize an abstract vnode.
 */
int
vnode_init(struct vnode *vn, const struct vnode_ops *ops,
	   struct fs *fs, void *fsdata)
{
	KASSERT(vn != NULL);
	KASSERT(ops != NULL);

	vn->vn_ops = ops;
	vn->vn_refcount = 1;
	vn->vn_opencount = 0;//spinlock_init(&vn->vn_countlock);
	vn->vn_fs = fs;
	vn->vn_data = fsdata;
	return 0;
}

/*
 * Destroy an abstract vnode.
 */
void
vnode_cleanup(struct vnode *vn)
{
	KASSERT(vn->vn_refcount == 1);

	KASSERT(vn->vn_opencount==0);//spinlock_cleanup(&vn->vn_countlock);

	vn->vn_ops = NULL;
	vn->vn_refcount = 0;
	vn->vn_opencount = 0;
	vn->vn_fs = NULL;
	vn->vn_data = NULL;
}


/*
 * Increment refcount.
 * Called by VOP_INCREF.
 */
void
vnode_incref(struct vnode *vn)
{
	KASSERT(vn != NULL);

	vfs_biglock_acquire();//spinlock_acquire(&vn->vn_countlock);
	vn->vn_refcount++;
	vfs_biglock_release();//spinlock_release(&vn->vn_countlock);
}

/*
 * Decrement refcount.
 * Called by VOP_DECREF.
 * Calls VOP_RECLAIM if the refcount hits zero.
 */
void
vnode_decref(struct vnode *vn)
{
	int result;
	KASSERT(vn != NULL);
	vfs_biglock_acquire();
	KASSERT(vn->vn_refcount>0);
	if (vn->vn_refcount>1) {
		vn->vn_refcount--;
	}
	else {
		result = VOP_RECLAIM(vn);
		if (result != 0 && result != EBUSY) {
			// XXX: lame.
			kprintf("vfs: Warning: VOP_RECLAIM: %s\n",
				strerror(result));
		}
	}
	vfs_biglock_release();
}

/*
 * Check for various things being valid.
 * Called before all VOP_* calls.
 */
void
vnode_check(struct vnode *v, const char *opstr)
{
	/* not safe, and not really needed to check constant fields */
	vfs_biglock_acquire();

	if (v == NULL) {
		panic("vnode_check: vop_%s: null vnode\n", opstr);
	}
	if (v == (void *)0xdeadbeef) {
		panic("vnode_check: vop_%s: deadbeef vnode\n", opstr);
	}

	if (v->vn_ops == NULL) {
		panic("vnode_check: vop_%s: null ops pointer\n", opstr);
	}
	if (v->vn_ops == (void *)0xdeadbeef) {
		panic("vnode_check: vop_%s: deadbeef ops pointer\n", opstr);
	}

	if (v->vn_ops->vop_magic != VOP_MAGIC) {
		panic("vnode_check: vop_%s: ops with bad magic number %lx\n",
		      opstr, v->vn_ops->vop_magic);
	}

	// Device vnodes have null fs pointers.
	//if (v->vn_fs == NULL) {
	//	panic("vnode_check: vop_%s: null fs pointer\n", opstr);
	//}
	if (v->vn_fs == (void *)0xdeadbeef) {
		panic("vnode_check: vop_%s: deadbeef fs pointer\n", opstr);
	}

	//spinlock_acquire(&v->vn_countlock);

	if (v->vn_refcount < 0) {
		panic("vnode_check: vop_%s: negative refcount %d\n", opstr,
		      v->vn_refcount);
	}
	else if (v->vn_refcount == 0 && strcmp(opstr, "reclaim")) {
		panic("vnode_check: vop_%s: zero refcount\n", opstr);
	}
	else if (v->vn_refcount > 0x100000) {
		kprintf("vnode_check: vop_%s: warning: large refcount %d\n",
			opstr, v->vn_refcount);
	}

	if (v->vn_opencount < 0) {
		panic("vnode_check: vop_%s: negative opencount %d\n", opstr,
		      v->vn_opencount);
	}
	else if (v->vn_opencount > 0x100000) {
		kprintf("vnode_check: vop_%s: warning: large opencount %d\n", 
			opstr, v->vn_opencount);
	}

	vfs_biglock_release();
}
