#include <linux/wait.h>

#include <linux/vfs/fs.h>
#include <linux/vfs/private/nameidata.h>
#include <linux/vfs/private/fs.h>

static inline int d_revalidate(struct dentry *dentry, unsigned int flags)
{
    if (unlikely(dentry->d_flags & DCACHE_OP_REVALIDATE))
        return dentry->d_op->d_revalidate(dentry, flags);
    else
        return 1;
}

static bool try_to_unlazy(struct nameidata *nd)
{
    pr_todo();
    return true;
}

static bool try_to_unlazy_next(struct nameidata *nd, struct dentry *dentry)
{
    pr_todo();
    return false;
}

static struct dentry *__lookup_slow(const struct qstr *name,
                                    struct dentry *dir,
                                    unsigned int flags)
{
    struct dentry *dentry, *old;
    struct inode *inode = dir->d_inode;
    DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

    /* Don't go there if it's already dead */
    if (unlikely(IS_DEADDIR(inode)))
        return ERR_PTR(-ENOENT);

again:
    dentry = d_alloc_parallel(dir, name, &wq);
    if (IS_ERR(dentry))
        return dentry;

    if (unlikely(!d_in_lookup(dentry)))
    {
        int error = d_revalidate(dentry, flags);
        if (unlikely(error <= 0))
        {
            if (!error)
            {
                d_invalidate(dentry);
                dput(dentry);
                goto again;
            }
            dput(dentry);
            dentry = ERR_PTR(error);
        }
    }
    else
    {
        old = inode->i_op->lookup(inode, dentry, flags);
        d_lookup_done(dentry);
        if (unlikely(old))
        {
            dput(dentry);
            dentry = old;
        }
    }

    return dentry;
}

static struct dentry *lookup_slow(const struct qstr *name,
                                  struct dentry *dir,
                                  unsigned int flags)
{
    struct inode *inode = dir->d_inode;
    struct dentry *res;

    inode_lock_shared(inode);
    res = __lookup_slow(name, dir, flags);
    inode_unlock_shared(inode);

    return res;
}

static struct dentry *lookup_fast(struct nameidata *nd)
{
    struct dentry *dentry, *parent = nd->path.dentry;
    int status = 1;

    /*
     * Rename seqlock is not required here because in the off chance
     * of a false negative due to a concurrent rename, the caller is
     * going to fall back to non-racy lookup.
     */
    if (nd->flags & LOOKUP_RCU)
    {
        dentry = __d_lookup_rcu(parent, &nd->last, &nd->next_seq);
        if (unlikely(!dentry))
        {
            if (!try_to_unlazy(nd))
                return ERR_PTR(-ECHILD);
            return NULL;
        }

        /*
         * This sequence count validates that the parent had no
         * changes while we did the lookup of the dentry above.
         */
        if (read_seqcount_retry(&parent->d_seq, nd->seq))
            return ERR_PTR(-ECHILD);

        status = d_revalidate(dentry, nd->flags);
        if (likely(status > 0))
            return dentry;
        if (!try_to_unlazy_next(nd, dentry))
            return ERR_PTR(-ECHILD);
        if (status == -ECHILD)
            /* we'd been told to redo it in non-rcu mode */
            status = d_revalidate(dentry, nd->flags);
    }
    else
    {
        dentry = __d_lookup(parent, &nd->last);
        if (unlikely(!dentry))
            return NULL;

        status = d_revalidate(dentry, nd->flags);
    }

    if (unlikely(status <= 0))
    {
        if (!status)
            d_invalidate(dentry);
        dput(dentry);

        return ERR_PTR(status);
    }

    return dentry;
}

/*
 * This looks up the name in dcache and possibly revalidates the found dentry.
 * NULL is returned if the dentry does not exist in the cache.
 */
static struct dentry *lookup_dcache(const struct qstr *name,
                                    struct dentry *dir,
                                    unsigned int flags)
{
    struct dentry *dentry = d_lookup(dir, name);

    if (dentry)
    {
        int error = d_revalidate(dentry, flags);
        if (unlikely(error <= 0))
        {
            if (!error)
                d_invalidate(dentry);
            dput(dentry);
            return ERR_PTR(error);
        }
    }

    return dentry;
}
