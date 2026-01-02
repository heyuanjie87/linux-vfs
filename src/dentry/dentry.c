#include <linux/vfs/fs.h>
#include <linux/vfs/private/dcache.h>

static void d_lru_add(struct dentry *dentry)
{
    pr_todo();
}

static void d_lru_del(struct dentry *dentry)
{
    pr_todo();
}

static void dentry_free(struct dentry *dentry)
{
    pr_todo();
}

static inline void dentry_unlist(struct dentry *dentry)
{
    pr_todo();
}

/*
 * Release the dentry's inode, using the filesystem
 * d_iput() operation if defined.
 */
static void dentry_unlink_inode(struct dentry * dentry)
{
    pr_todo();
}

static struct dentry *__dentry_kill(struct dentry *dentry)
{
    struct dentry *parent = NULL;
    bool can_free = true;

    /*
     * The dentry is now unrecoverably dead to the world.
     */
    lockref_mark_dead(&dentry->d_lockref);

    /*
     * inform the fs via d_prune that this dentry is about to be
     * unhashed and destroyed.
     */
    if (dentry->d_flags & DCACHE_OP_PRUNE)
        dentry->d_op->d_prune(dentry);

    if (dentry->d_flags & DCACHE_LRU_LIST)
    {
        if (!(dentry->d_flags & DCACHE_SHRINK_LIST))
            d_lru_del(dentry);
    }
    /* if it was on the hash then remove it */
    __d_drop(dentry);
    if (dentry->d_inode)
        dentry_unlink_inode(dentry);
    else
        d_unlock(dentry);

    if (dentry->d_op && dentry->d_op->d_release)
        dentry->d_op->d_release(dentry);

    cond_resched();
    /* now that it's negative, ->d_parent is stable */
    if (!IS_ROOT(dentry))
    {
        parent = dentry->d_parent;
        d_lock(parent);
    }
    d_lock_nested(dentry, DENTRY_D_LOCK_NESTED);
    dentry_unlist(dentry);
    if (dentry->d_flags & DCACHE_SHRINK_LIST)
        can_free = false;
    d_unlock(dentry);
    if (likely(can_free))
        dentry_free(dentry);
    if (parent && --parent->d_lockref.count)
    {
        d_unlock(parent);
        return NULL;
    }
    return parent;
}

/*
 * Lock a dentry for feeding it to __dentry_kill().
 * Called under rcu_read_lock() and dentry->d_lock; the former
 * guarantees that nothing we access will be freed under us.
 * Note that dentry is *not* protected from concurrent dentry_kill(),
 * d_delete(), etc.
 *
 * Return false if dentry is busy.  Otherwise, return true and have
 * that dentry's inode locked.
 */

static bool lock_for_kill(struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;

    if (unlikely(dentry->d_lockref.count))
        return false;

    if (!inode || likely(spin_trylock(&inode->i_lock)))
        return true;

    do
    {
        d_unlock(dentry);
        spin_lock(&inode->i_lock);
        d_lock(dentry);
        if (likely(inode == dentry->d_inode))
            break;
        spin_unlock(&inode->i_lock);
        inode = dentry->d_inode;
    } while (inode);
    if (likely(!dentry->d_lockref.count))
        return true;
    if (inode)
        spin_unlock(&inode->i_lock);

    return false;
}

/*
 * Decide if dentry is worth retaining.  Usually this is called with dentry
 * locked; if not locked, we are more limited and might not be able to tell
 * without a lock.  False in this case means "punt to locked path and recheck".
 *
 * In case we aren't locked, these predicates are not "stable". However, it is
 * sufficient that at some point after we dropped the reference the dentry was
 * hashed and the flags had the proper value. Other dentry users may have
 * re-gotten a reference to the dentry and change that, but our work is done -
 * we can leave the dentry around with a zero refcount.
 */
static inline bool retain_dentry(struct dentry *dentry, bool locked)
{
    unsigned int d_flags;

    smp_rmb();
    d_flags = READ_ONCE(dentry->d_flags);

    // Unreachable? Nobody would be able to look it up, no point retaining
    if (unlikely(d_unhashed(dentry)))
        return false;

    // Same if it's disconnected
    if (unlikely(d_flags & DCACHE_DISCONNECTED))
        return false;

    // ->d_delete() might tell us not to bother, but that requires
    // ->d_lock; can't decide without it
    if (unlikely(d_flags & DCACHE_OP_DELETE))
    {
        if (!locked || dentry->d_op->d_delete(dentry))
            return false;
    }

    // Explicitly told not to bother
    if (unlikely(d_flags & DCACHE_DONTCACHE))
        return false;

    // At this point it looks like we ought to keep it.  We also might
    // need to do something - put it on LRU if it wasn't there already
    // and mark it referenced if it was on LRU, but not marked yet.
    // Unfortunately, both actions require ->d_lock, so in lockless
    // case we'd have to punt rather than doing those.
    if (unlikely(!(d_flags & DCACHE_LRU_LIST)))
    {
        if (!locked)
            return false;
        d_lru_add(dentry);
    }
    else if (unlikely(!(d_flags & DCACHE_REFERENCED)))
    {
        if (!locked)
            return false;
        dentry->d_flags |= DCACHE_REFERENCED;
    }
    return true;
}

/*
 * Try to do a lockless dput(), and return whether that was successful.
 *
 * If unsuccessful, we return false, having already taken the dentry lock.
 * In that case refcount is guaranteed to be zero and we have already
 * decided that it's not worth keeping around.
 *
 * The caller needs to hold the RCU read lock, so that the dentry is
 * guaranteed to stay around even if the refcount goes down to zero!
 */
static inline bool fast_dput(struct dentry *dentry)
{
    int ret;

    /*
     * try to decrement the lockref optimistically.
     */
    ret = lockref_put_return(&dentry->d_lockref);

    /*
     * If the lockref_put_return() failed due to the lock being held
     * by somebody else, the fast path has failed. We will need to
     * get the lock, and then check the count again.
     */
    if (unlikely(ret < 0))
    {
        d_lock(dentry);
        if (WARN_ON_ONCE(dentry->d_lockref.count <= 0))
        {
            d_unlock(dentry);
            return true;
        }

        dentry->d_lockref.count--;
        goto locked;
    }

    /*
     * If we weren't the last ref, we're done.
     */
    if (ret)
        return true;

    /*
     * Can we decide that decrement of refcount is all we needed without
     * taking the lock?  There's a very common case when it's all we need -
     * dentry looks like it ought to be retained and there's nothing else
     * to do.
     */
    if (retain_dentry(dentry, false))
        return true;

    /*
     * Either not worth retaining or we can't tell without the lock.
     * Get the lock, then.  We've already decremented the refcount to 0,
     * but we'll need to re-check the situation after getting the lock.
     */
    d_lock(dentry);

    /*
     * Did somebody else grab a reference to it in the meantime, and
     * we're no longer the last user after all? Alternatively, somebody
     * else could have killed it and marked it dead. Either way, we
     * don't need to do anything else.
     */
locked:
    if (dentry->d_lockref.count || retain_dentry(dentry, true))
    {
        d_unlock(dentry);
        return true;
    }

    return false;
}

/*
 * dput - release a dentry
 * @dentry: dentry to release
 *
 * Release a dentry. This will drop the usage count and if appropriate
 * call the dentry unlink method as well as removing it from the queues and
 * releasing its resources. If the parent dentries were scheduled for release
 * they too may now get deleted.
 */
void dput(struct dentry *dentry)
{
    if (!dentry)
        return;

    might_sleep();
    rcu_read_lock();
    if (likely(fast_dput(dentry)))
    {
        rcu_read_unlock();
        return;
    }

    while (lock_for_kill(dentry))
    {
        rcu_read_unlock();
        dentry = __dentry_kill(dentry);
        if (!dentry)
            return;

        if (retain_dentry(dentry, true))
        {
            d_unlock(dentry);
            return;
        }
        rcu_read_lock();
    }
    rcu_read_unlock();
    d_unlock(dentry);
}

void d_invalidate(struct dentry *d)
{
    pr_todo();
}

void d_lookup_done(struct dentry *dentry)
{
    pr_todo();
}

/* This must be called with d_lock held */
static inline void __dget_dlock(struct dentry *dentry)
{
    pr_todo();
}

void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
{
    pr_todo();
}

bool d_is_miss(const struct dentry *dentry)
{
    return __d_entry_type(dentry) == DCACHE_MISS_TYPE;
}

bool d_is_negative(const struct dentry *dentry)
{
    return d_is_miss(dentry);
}

bool d_is_positive(const struct dentry *dentry)
{
    return !d_is_negative(dentry);
}

struct dentry *d_splice_alias(struct inode *inode, struct dentry *dentry)
{
    pr_todo();
    return NULL;
}

void dont_mount(struct dentry *dentry)
{
    d_lock(dentry);
    dentry->d_flags |= DCACHE_CANT_MOUNT;
    d_unlock(dentry);
}
