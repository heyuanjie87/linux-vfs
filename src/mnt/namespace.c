/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/vfs/private/fs.h>

struct ns_hashtable
{
    struct hlist_head *hashtable;
    unsigned int mask;
    unsigned int shift;
};

DEFINE_SEQLOCK(mount_lock);
static LIST_HEAD(ex_mountpoints); /* protected by namespace_sem */

static struct ns_hashtable mountpoint_hashtable = {};
static struct ns_hashtable mount_hashtable = {};

static DECLARE_RWSEM(namespace_sem);

enum mnt_tree_flags_t
{
    MNT_TREE_MOVE = BIT(0),
    MNT_TREE_BENEATH = BIT(1),
};

static inline struct hlist_head *__hash_head(struct ns_hashtable *ht, void *key1, void *key2)
{
    uintptr_t tmp = ((uintptr_t)key1 / 64);
    tmp += ((uintptr_t)key2 / 64);

    tmp = tmp + (tmp >> ht->shift);
    tmp = tmp & ht->mask;

    return &ht->hashtable[tmp];
}

static inline struct hlist_head *mp_hash(struct dentry *dentry)
{
    return __hash_head(&mountpoint_hashtable, dentry, 0);
}

static inline struct hlist_head *m_hash(struct vfsmount *mnt, struct dentry *dentry)
{
    return __hash_head(&mount_hashtable, mnt, dentry);
}

static inline void lock_mount_hash(void)
{
    write_seqlock(&mount_lock);
}

static inline void unlock_mount_hash(void)
{
    write_sequnlock(&mount_lock);
}

static inline void namespace_lock(void)
{
    down_write(&namespace_sem);
}

static inline void namespace_unlock(void)
{
    up_write(&namespace_sem);
}

static int mnt_alloc_id(struct mount *mnt)
{
    pr_todo();
    return 0;
}

static struct mount *alloc_vfsmnt(const char *name)
{
    struct mount *mnt = kzalloc(sizeof(struct mount), GFP_KERNEL);

    if (mnt)
    {
        int err;

        err = mnt_alloc_id(mnt);

        INIT_LIST_HEAD(&mnt->mnt_mounts);
        INIT_LIST_HEAD(&mnt->mnt_child);
        INIT_LIST_HEAD(&mnt->mnt_instance);
    }

    return mnt;
}

struct mount *__lookup_mnt(struct vfsmount *mnt, struct dentry *dentry)
{
    struct hlist_head *head = m_hash(mnt, dentry);
    struct mount *p;

    hlist_for_each_entry(p, head, mnt_hash)
    {
        if (&p->mnt_parent->mnt == mnt && p->mnt_mountpoint == dentry)
            return p;
    }

    return NULL;
}

struct vfsmount *lookup_mnt(const struct path *path)
{
    struct mount *child_mnt;
    struct vfsmount *m;

    pr_todo();

    child_mnt = __lookup_mnt(path->mnt, path->dentry);
    m = child_mnt ? &child_mnt->mnt : NULL;

    return m;
}

static struct mountpoint *lookup_mountpoint(struct dentry *dentry)
{
    struct hlist_head *chain = mp_hash(dentry);
    struct mountpoint *mp;

    hlist_for_each_entry(mp, chain, m_hash)
    {
        if (mp->m_dentry == dentry)
        {
            mp->m_count++;
            return mp;
        }
    }
    return NULL;
}

static int get_mountpoint(struct dentry *dentry, struct mountpoint **mp_ret)
{
    struct mountpoint *mp, *new = NULL;
    int ret;

    if (d_mountpoint(dentry))
    {
        /* might be worth a WARN_ON() */
        if (d_unlinked(dentry))
            return -ENOENT;

    mountpoint:
        read_seqlock_excl(&mount_lock);
        mp = lookup_mountpoint(dentry);
        read_sequnlock_excl(&mount_lock);
        if (mp)
            goto done;
    }

    if (!new)
        new = kmalloc(sizeof(struct mountpoint), GFP_KERNEL);
    if (!new)
        return -ENOMEM;

    /* Exactly one processes may set d_mounted */
    ret = d_set_mounted(dentry);

    /* Someone else set d_mounted? */
    if (ret == -EBUSY)
        goto mountpoint;

    /* The dentry is not available as a mountpoint? */
    mp = ERR_PTR(ret);
    if (ret)
        goto done;

    /* Add the new mountpoint to the hash table */
    read_seqlock_excl(&mount_lock);
    new->m_dentry = dget(dentry);
    new->m_count = 1;
    hlist_add_head(&new->m_hash, mp_hash(dentry));
    INIT_HLIST_HEAD(&new->m_list);
    read_sequnlock_excl(&mount_lock);

    mp = new;
    new = NULL;
done:
    kfree(new);
    *mp_ret = mp;

    return ret;
}

/**
 * do_lock_mount - lock mount and mountpoint
 * @path:    target path
 * @beneath: whether the intention is to mount beneath @path
 *
 * Follow the mount stack on @path until the top mount @mnt is found. If
 * the initial @path->{mnt,dentry} is a mountpoint lookup the first
 * mount stacked on top of it. Then simply follow @{mnt,mnt->mnt_root}
 * until nothing is stacked on top of it anymore.
 *
 * Acquire the inode_lock() on the top mount's ->mnt_root to protect
 * against concurrent removal of the new mountpoint from another mount
 * namespace.
 *
 * If @beneath is requested, acquire inode_lock() on @mnt's mountpoint
 * @mp on @mnt->mnt_parent must be acquired. This protects against a
 * concurrent unlink of @mp->mnt_dentry from another mount namespace
 * where @mnt doesn't have a child mount mounted @mp. A concurrent
 * removal of @mnt->mnt_root doesn't matter as nothing will be mounted
 * on top of it for @beneath.
 *
 * In addition, @beneath needs to make sure that @mnt hasn't been
 * unmounted or moved from its current mountpoint in between dropping
 * @mount_lock and acquiring @namespace_sem. For the !@beneath case @mnt
 * being unmounted would be detected later by e.g., calling
 * check_mnt(mnt) in the function it's called from. For the @beneath
 * case however, it's useful to detect it directly in do_lock_mount().
 * If @mnt hasn't been unmounted then @mnt->mnt_mountpoint still points
 * to @mnt->mnt_mp->m_dentry. But if @mnt has been unmounted it will
 * point to @mnt->mnt_root and @mnt->mnt_mp will be NULL.
 *
 * Return: Either the target mountpoint on the top mount or the top
 *         mount's mountpoint.
 */
static struct mountpoint *do_lock_mount(struct path *path, bool beneath)
{
    struct vfsmount *mnt = path_mnt(path);
    struct dentry *dentry;
    struct mountpoint *mp = ERR_PTR(-ENOENT);
    int error;

    for (;;)
    {
        struct mount *m;

        if (beneath)
        {
            m = real_mount(mnt);
            read_seqlock_excl(&mount_lock);
            dentry = dget(m->mnt_mountpoint);
            read_sequnlock_excl(&mount_lock);
        }
        else
        {
            dentry = path->dentry;
        }

        inode_lock(dentry->d_inode);
        if (unlikely(cant_mount(dentry)))
        {
            inode_unlock(dentry->d_inode);
            goto out;
        }

        namespace_lock();

        if (beneath && (!is_mounted(mnt) || m->mnt_mountpoint != dentry))
        {
            namespace_unlock();
            inode_unlock(dentry->d_inode);
            goto out;
        }

        mnt = lookup_mnt(path);
        if (likely(!mnt))
            break;

        namespace_unlock();
        inode_unlock(dentry->d_inode);
        if (beneath)
            dput(dentry);
        path_put(path);
        path->mnt = mnt;
        path->dentry = dget(mnt->mnt_root);
    }

    error = get_mountpoint(dentry, &mp);
    if (error)
    {
        namespace_unlock();
        inode_unlock(dentry->d_inode);
        mp = ERR_PTR(error);
    }

out:
    if (beneath)
        dput(dentry);

    return mp;
}

static inline struct mountpoint *lock_mount(struct path *path)
{
    return do_lock_mount(path, false);
}

/*
 * vfsmount lock must be held.  Additionally, the caller is responsible
 * for serializing calls for given disposal list.
 */
static void __put_mountpoint(struct mountpoint *mp, struct list_head *list)
{
    if (!--mp->m_count)
    {
        struct dentry *dentry = mp->m_dentry;
        BUG_ON(!hlist_empty(&mp->m_list));
        d_clear_mounted(dentry);
        dput_to_list(dentry, list);
        hlist_del(&mp->m_hash);
        kfree(mp);
    }
}

/* called with namespace_lock and vfsmount lock */
static void put_mountpoint(struct mountpoint *mp)
{
    __put_mountpoint(mp, &ex_mountpoints);
}

static void unlock_mount(struct mountpoint *where)
{
    struct dentry *dentry = where->m_dentry;

    read_seqlock_excl(&mount_lock);
    put_mountpoint(where);
    read_sequnlock_excl(&mount_lock);

    namespace_unlock();
    inode_unlock(dentry->d_inode);
}

struct vfsmount *vfs_create_mount(struct fs_context *fc)
{
    struct mount *mnt;

    if (!fc->root)
        return ERR_PTR(-EINVAL);

    mnt = alloc_vfsmnt(fc->source ?: "none");
    if (!mnt)
        return ERR_PTR(-ENOMEM);

    if (fc->sb_flags & SB_KERNMOUNT)
        mnt->mnt.mnt_flags = MNT_INTERNAL;

    atomic_inc(&fc->root->d_sb->s_active);
    mnt->mnt.mnt_sb = fc->root->d_sb;
    mnt->mnt.mnt_root = dget(fc->root);
    mnt->mnt_mountpoint = mnt->mnt.mnt_root;
    mnt->mnt_parent = mnt;

    lock_mount_hash();
    list_add_tail(&mnt->mnt_instance, &mnt->mnt.mnt_sb->s_mounts);
    unlock_mount_hash();
    return &mnt->mnt;
}

static inline void mnt_add_count(struct mount *mnt, int n)
{
    mnt->mnt_count += n;
}

/*
 * vfsmount lock must be held for write
 */
void mnt_set_mountpoint(struct mount *mnt,
			struct mountpoint *mp,
			struct mount *child_mnt)
{
	mp->m_count++;
	mnt_add_count(mnt, 1);	/* essentially, that's mntget */
	child_mnt->mnt_mountpoint = mp->m_dentry;
	child_mnt->mnt_parent = mnt;
	child_mnt->mnt_mp = mp;
	hlist_add_head(&child_mnt->mnt_mp_list, &mp->m_list);
}

static void __attach_mnt(struct mount *mnt, struct mount *parent)
{
	hlist_add_head_rcu(&mnt->mnt_hash,
			   m_hash(&parent->mnt, mnt->mnt_mountpoint));
	list_add_tail(&mnt->mnt_child, &parent->mnt_mounts);
}

static void commit_tree(struct mount *mnt)
{
    struct mount *parent = mnt->mnt_parent;

    __attach_mnt(mnt, parent);
}

static int attach_recursive_mnt(struct mount *source_mnt,
                                struct mount *top_mnt,
                                struct mountpoint *dest_mp,
                                enum mnt_tree_flags_t flags)
{
    struct mount *child, *dest_mnt, *p;
    struct mountpoint *smp;
    int err;
    bool moving = flags & MNT_TREE_MOVE, beneath = flags & MNT_TREE_BENEATH;

    pr_todo();

    err = get_mountpoint(source_mnt->mnt.mnt_root, &smp);
    if (err == 0)
    {
        if (beneath)
            dest_mnt = top_mnt->mnt_parent;
        else
            dest_mnt = top_mnt;

        if (moving)
        {

        }
        else
        {
            if (beneath)
            {

            }
		    else
            {
                mnt_set_mountpoint(dest_mnt, dest_mp, source_mnt);
            }

            commit_tree(source_mnt);
        }
    }

out:
    return err;
}

static int graft_tree(struct mount *mnt, struct mount *p, struct mountpoint *mp)
{
    if (mnt->mnt.mnt_sb->s_flags & SB_NOUSER)
        return -EINVAL;

    if (d_is_dir(mp->m_dentry) !=
        d_is_dir(mnt->mnt.mnt_root))
        return -ENOTDIR;

    return attach_recursive_mnt(mnt, p, mp, 0);
}

/*
 * add a mount into a namespace's mount tree
 */
static int do_add_mount(struct mount *newmnt, struct mountpoint *mp,
                        const struct path *path, int mnt_flags)
{
    struct mount *parent = real_mount(path->mnt);

    mnt_flags &= ~MNT_INTERNAL_FLAGS;

    return graft_tree(newmnt, parent, mp);
}

/*
 * Create a new mount using a superblock configuration and request it
 * be added to the namespace tree.
 */
static int do_new_mount_fc(struct fs_context *fc, struct path *mountpoint,
                           unsigned int mnt_flags)
{
    struct vfsmount *mnt;
    struct mountpoint *mp;
    struct super_block *sb = fc->root->d_sb;
    int error;

    pr_todo();

    up_write(&sb->s_umount);

    mnt = vfs_create_mount(fc);
    if (IS_ERR(mnt))
        return PTR_ERR(mnt);

    mp = lock_mount(mountpoint);
    if (IS_ERR(mp))
    {
        mntput(mnt);
        return PTR_ERR(mp);
    }

    error = do_add_mount(real_mount(mnt), mp, mountpoint, mnt_flags);
    unlock_mount(mp);

    if (error < 0)
        mntput(mnt);

    return error;
}

/*
 * create a new mount for userspace and request it to be added into the
 * namespace's tree
 */
static int do_new_mount(filedesc_t *fdp, struct path *path, const char *fstype, int sb_flags,
                        int mnt_flags, const char *name, void *data)
{
    struct file_system_type *type;
    struct fs_context *fc;
    const char *subtype = NULL;
    int err = 0;

    if (!fstype)
        return -EINVAL;

    type = get_fs_type(fstype);
    if (!type)
        return -ENODEV;

    if (type->fs_flags & FS_HAS_SUBTYPE)
    {
        subtype = strchr(fstype, '.');
        if (subtype)
        {
            subtype++;
            if (!*subtype)
            {
                put_filesystem(type);
                return -EINVAL;
            }
        }
    }

    fc = fs_context_for_mount(type, sb_flags);
    put_filesystem(type);
    if (IS_ERR(fc))
        return PTR_ERR(fc);

    fc->fc_fdp = fdp;

    if (subtype)
        err = vfs_parse_fs_string(fc, "subtype",
                                  subtype, strlen(subtype));
    if (!err && name)
        err = vfs_parse_fs_string(fc, "source", name, strlen(name));
    if (!err)
        err = parse_monolithic_mount_data(fc, data);
    if (!err && !mount_capable(fc))
        err = -EPERM;
    if (!err)
        err = vfs_get_tree(fc);
    if (!err)
        err = do_new_mount_fc(fc, path, mnt_flags);

    put_fs_context(fc);

    return err;
}

int path_mount(filedesc_t *fdp, const char *dev_name, struct path *path,
               const char *type_page, unsigned long flags, void *data_page)
{
    unsigned int mnt_flags = 0, sb_flags;
    int ret;

    /* Discard magic */
    if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
        flags &= ~MS_MGC_MSK;

    /* Basic sanity checks */
    if (data_page)
        ((char *)data_page)[0] = 0; // TODO

    if (flags & MS_NOUSER)
        return -EINVAL;

    /* Default to relatime unless overriden */
    if (!(flags & MS_NOATIME))
        mnt_flags |= MNT_RELATIME;

    /* Separate the per-mountpoint flags */
    if (flags & MS_NOSUID)
        mnt_flags |= MNT_NOSUID;
    if (flags & MS_NODEV)
        mnt_flags |= MNT_NODEV;
    if (flags & MS_NOEXEC)
        mnt_flags |= MNT_NOEXEC;
    if (flags & MS_NOATIME)
        mnt_flags |= MNT_NOATIME;
    if (flags & MS_NODIRATIME)
        mnt_flags |= MNT_NODIRATIME;
    if (flags & MS_STRICTATIME)
        mnt_flags &= ~(MNT_RELATIME | MNT_NOATIME);
    if (flags & MS_RDONLY)
        mnt_flags |= MNT_READONLY;
    if (flags & MS_NOSYMFOLLOW)
        mnt_flags |= MNT_NOSYMFOLLOW;

    /* The default atime for remount is preservation */
    if ((flags & MS_REMOUNT) &&
        ((flags & (MS_NOATIME | MS_NODIRATIME | MS_RELATIME |
                   MS_STRICTATIME)) == 0))
    {
        mnt_flags &= ~MNT_ATIME_MASK;
        mnt_flags |= path->mnt->mnt_flags & MNT_ATIME_MASK;
    }

    sb_flags = flags & (SB_RDONLY |
                        SB_SYNCHRONOUS |
                        SB_MANDLOCK |
                        SB_DIRSYNC |
                        SB_SILENT |
                        SB_POSIXACL |
                        SB_LAZYTIME |
                        SB_I_VERSION);

    ret = do_new_mount(fdp, path, type_page, sb_flags, mnt_flags, dev_name,
                       data_page);

    return ret;
}

struct vfsmount *fc_mount(struct fs_context *fc)
{
    int err = vfs_get_tree(fc);
    if (!err)
    {
        up_write(&fc->root->d_sb->s_umount);
        return vfs_create_mount(fc);
    }
    return ERR_PTR(err);
}

bool is_local_mountpoint(struct dentry *dentry)
{
    pr_todo();

    return false;
}

void detach_mounts(struct dentry *dentry)
{
    pr_todo();
}

static int can_umount(const struct path *path, int flags)
{
    pr_todo();
    return 0;
}

static void mntput_no_expire(struct mount *mnt)
{
    pr_todo();
}

static int do_umount(struct mount *mnt, int flags)
{
    pr_todo();
    return 0;
}

int path_umount(struct path *path, int flags)
{
    struct mount *mnt = real_mount(path->mnt);
    int ret;

    ret = can_umount(path, flags);
    if (!ret)
        ret = do_umount(mnt, flags);

    /* we mustn't call path_put() as that would clear mnt_expiry_mark */
    dput(path->dentry);
    mntput_no_expire(mnt);
    return ret;
}

static int __hashtable_init(struct ns_hashtable *ht, unsigned int count)
{
    ht->hashtable = kcalloc(count, sizeof(struct hlist_head), GFP_KERNEL);
    if (!ht->hashtable)
        return -ENOMEM;

    ht->shift = __ilog2_u32(count);
    ht->mask = (1 << ht->shift) - 1;

    return 0;
}

int mnt_init(void)
{
    int ret;

    ret = __hashtable_init(&mountpoint_hashtable, 16);
    ret = __hashtable_init(&mount_hashtable, 16);

    return ret;
}
