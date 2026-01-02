#include <linux/vfs/fs.h>
#include <linux/vfs/private/dcache.h>
#include <linux/rculist_bl.h>
#include <linux/log2.h>
#include <linux/stringhash.h>

struct dcache _dcache; // TODO

static inline struct hlist_bl_head *__d_hash(struct dcache *cache, unsigned int hash)
{
    return cache->hashtable + (hash >> cache->hash_shift);
}

static inline struct hlist_bl_head *d_hash(unsigned int hash)
{
    return __d_hash(&_dcache, hash);
}

/**
 * d_same_name - compare dentry name with case-exact name
 * @parent: parent dentry
 * @dentry: the negative dentry that was passed to the parent's lookup func
 * @name:   the case-exact name to be associated with the returned dentry
 *
 * Return: true if names are same, or false
 */
bool d_same_name(const struct dentry *dentry, const struct dentry *parent,
                 const struct qstr *name)
{
    bool ret;

    if (dentry->d_name.len != name->len)
    {
        ret = false;
    }
    else
    {
        ret = !strcmp(name->name, dentry->d_name.name);
    }

    return ret;
}

int d_unhashed(const struct dentry *dentry)
{
    return hlist_bl_unhashed(&dentry->d_hash);
}

int d_unlinked(const struct dentry *dentry)
{
    return d_unhashed(dentry) && !IS_ROOT(dentry);
}

void shrink_dcache_parent(struct dentry *parent)
{
    pr_todo();
}

int dcache_init(struct dcache *c, unsigned count)
{
    int err = 0;

    c->hashtable = kcalloc(count, sizeof(struct hlist_bl_head), GFP_KERNEL);
    if (c->hashtable)
    {
        c->hash_shift = 32 - __ilog2_u32(count);

        init_rwsem(&c->lock);
    }
    else
    {
        err = -ENOMEM;
    }

    return err;
}

static void __d_rehash(struct dentry *entry)
{
    struct hlist_bl_head *b = d_hash(entry->d_name.hash);

    hlist_bl_lock(b);
    hlist_bl_add_head_rcu(&entry->d_hash, b);
    hlist_bl_unlock(b);
}

static inline void __d_set_inode_and_type(struct dentry *dentry,
                                          struct inode *inode,
                                          unsigned type_flags)
{
    unsigned flags;

    dentry->d_inode = inode;
    flags = READ_ONCE(dentry->d_flags);
    flags &= ~DCACHE_ENTRY_TYPE;
    flags |= type_flags;
    WRITE_ONCE(dentry->d_flags, flags);
}

static unsigned d_flags_for_inode(struct inode *inode)
{
    unsigned add_flags = DCACHE_REGULAR_TYPE;

    if (inode)
    {
        if (S_ISDIR(inode->i_mode))
        {
            add_flags = DCACHE_DIRECTORY_TYPE;
        }
        else if (S_ISLNK(inode->i_mode))
        {
            add_flags = DCACHE_SYMLINK_TYPE;
        }
        else if (!S_ISREG(inode->i_mode))
        {
            add_flags = DCACHE_SPECIAL_TYPE;
        }
    }
    else
    {
        add_flags = DCACHE_MISS_TYPE;
    }

    return add_flags;
}

/* inode->i_lock held if inode is non-NULL */

static inline void __d_add(struct dentry *dentry, struct inode *inode)
{
    d_lock(dentry);

    if (inode)
    {
        unsigned add_flags = d_flags_for_inode(inode);

        __d_set_inode_and_type(dentry, inode, add_flags);
    }

    __d_rehash(dentry);

    d_unlock(dentry);
}

void d_add(struct dentry *entry, struct inode *inode)
{
    pr_todo();

    __d_add(entry, inode);
}

static struct dentry *__d_lookup_rcu_op_compare(
    const struct dentry *parent,
    const struct qstr *name,
    unsigned *seqp)
{
    pr_todo();

    return NULL;
}

static inline int dentry_string_cmp(const char *cs, const char *ct, unsigned tcount)
{
    do
    {
        if (*cs != *ct)
            return 1;
        cs++;
        ct++;
        tcount--;
    } while (tcount);
    return 0;
}

static inline int dentry_cmp(const struct dentry *dentry, const char *ct, unsigned tcount)
{
    /*
     * Be careful about RCU walk racing with rename:
     * use 'READ_ONCE' to fetch the name pointer.
     *
     * NOTE! Even if a rename will mean that the length
     * was not loaded atomically, we don't care. The
     * RCU walk will check the sequence count eventually,
     * and catch it. And we won't overrun the buffer,
     * because we're reading the name pointer atomically,
     * and a dentry name is guaranteed to be properly
     * terminated with a NUL byte.
     *
     * End result: even if 'len' is wrong, we'll exit
     * early because the data cannot match (there can
     * be no NUL in the ct/tcount data)
     */
    const char *cs = READ_ONCE(dentry->d_name.name);

    return dentry_string_cmp(cs, ct, tcount);
}

/**
 * __d_lookup_rcu - search for a dentry (racy, store-free)
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * @seqp: returns d_seq value at the point where the dentry was found
 * Returns: dentry, or NULL
 *
 * __d_lookup_rcu is the dcache lookup function for rcu-walk name
 * resolution (store-free path walking) design described in
 * Documentation/filesystems/path-lookup.txt.
 *
 * This is not to be used outside core vfs.
 *
 * __d_lookup_rcu must only be used in rcu-walk mode, ie. with vfsmount lock
 * held, and rcu_read_lock held. The returned dentry must not be stored into
 * without taking d_lock and checking d_seq sequence count against @seq
 * returned here.
 *
 * A refcount may be taken on the found dentry with the d_rcu_to_refcount
 * function.
 *
 * Alternatively, __d_lookup_rcu may be called again to look up the child of
 * the returned dentry, so long as its parent's seqlock is checked after the
 * child is looked up. Thus, an interlocking stepping of sequence lock checks
 * is formed, giving integrity down the path walk.
 *
 * NOTE! The caller *has* to check the resulting dentry against the sequence
 * number we've returned before using any of the resulting dentry state!
 */
struct dentry *__d_lookup_rcu(const struct dentry *parent,
                              const struct qstr *name,
                              unsigned *seqp)
{
    u64 hashlen = name->hash_len;
    const char *str = name->name;
    struct hlist_bl_head *b = d_hash(hashlen_hash(hashlen));
    struct hlist_bl_node *node;
    struct dentry *dentry, *found = NULL;

    /*
     * Note: There is significant duplication with __d_lookup_rcu which is
     * required to prevent single threaded performance regressions
     * especially on architectures where smp_rmb (in seqcounts) are costly.
     * Keep the two functions in sync.
     */

    if (unlikely(parent->d_flags & DCACHE_OP_COMPARE))
        return __d_lookup_rcu_op_compare(parent, name, seqp);

    /*
     * The hash list is protected using RCU.
     *
     * Carefully use d_seq when comparing a candidate dentry, to avoid
     * races with d_move().
     *
     * It is possible that concurrent renames can mess up our list
     * walk here and result in missing our dentry, resulting in the
     * false-negative result. d_lookup() protects against concurrent
     * renames using rename_lock seqlock.
     *
     * See Documentation/filesystems/path-lookup.txt for more details.
     */
    hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash)
    {
        unsigned seq;

        /*
         * The dentry sequence count protects us from concurrent
         * renames, and thus protects parent and name fields.
         *
         * The caller must perform a seqcount check in order
         * to do anything useful with the returned dentry.
         *
         * NOTE! We do a "raw" seqcount_begin here. That means that
         * we don't wait for the sequence count to stabilize if it
         * is in the middle of a sequence change. If we do the slow
         * dentry compare, we will do seqretries until it is stable,
         * and if we end up with a successful lookup, we actually
         * want to exit RCU lookup anyway.
         *
         * Note that raw_seqcount_begin still *does* smp_rmb(), so
         * we are still guaranteed NUL-termination of ->d_name.name.
         */
        seq = raw_seqcount_begin(&dentry->d_seq);
        if (dentry->d_parent != parent)
            continue;
        if (d_unhashed(dentry))
            continue;
        if (dentry->d_name.hash_len != hashlen)
            continue;
        if (dentry_cmp(dentry, str, hashlen_len(hashlen)) != 0)
            continue;
        *seqp = seq;
        found = dentry;
        break;
    }

    return found;
}

void d_move(struct dentry *dentry, struct dentry *target)
{
    pr_todo();
}

struct dentry *d_find_alias(struct inode *inode)
{
    struct dentry *de = NULL;
    pr_todo();
    return de;
}

bool is_subdir(struct dentry *new_dentry, struct dentry *old_dentry)
{
    pr_todo();
    return false;
}

int d_set_mounted(struct dentry *dentry)
{
    pr_todo();
    dentry->d_flags |= DCACHE_MOUNTED;

    return 0;
}

void d_clear_mounted(struct dentry *dentry)
{
    d_lock(dentry);
    dentry->d_flags &= ~DCACHE_MOUNTED;
    d_unlock(dentry);
}

void dput_to_list(struct dentry *dentry, struct list_head *list)
{
    pr_todo();
}

static void __d_instantiate(struct dentry *dentry, struct inode *inode)
{
    unsigned add_flags;

    add_flags = d_flags_for_inode(inode);
    pr_todo();
    dentry->d_flags |= add_flags;
    dentry->d_inode = inode;
}

void d_instantiate(struct dentry *entry, struct inode *inode)
{
    pr_todo();

    if (inode)
    {
        __d_instantiate(entry, inode);
    }
}

void __d_drop(struct dentry *dentry)
{
    pr_todo();
}
