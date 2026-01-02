#include <linux/vfs/private/fs.h>

/**
 * dget - get a reference to a dentry
 * @dentry: dentry to get a reference to
 *
 * Given a dentry or %NULL pointer increment the reference count
 * if appropriate and return the dentry.  A dentry will not be
 * destroyed when it has references.  Conversely, a dentry with
 * no references can disappear for any number of reasons, starting
 * with memory pressure.  In other words, that primitive is
 * used to clone an existing reference; using it on something with
 * zero refcount is a bug.
 *
 * NOTE: it will spin if @dentry->d_lock is held.  From the deadlock
 * avoidance point of view it is equivalent to spin_lock()/increment
 * refcount/spin_unlock(), so calling it under @dentry->d_lock is
 * always a bug; so's calling it under ->d_lock on any of its descendents.
 *
 */
struct dentry *dget(struct dentry *dentry)
{
    if (dentry)
        lockref_get(&dentry->d_lockref);

    return dentry;
}

struct dentry *dget_parent(struct dentry *dentry)
{
    struct dentry *ret;

    ret = dentry->d_parent;

    return ret;
}
