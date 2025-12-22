#include <linux/wait.h>

#include <linux/vfs/fs.h>

#include <linux/delayed_call.h>
#include <linux/stringhash.h>

#include <linux/vfs/private/namei.h>
#include <linux/vfs/private/dcache.h>
#include <linux/vfs/cwd.h>
#include <linux/vfs/private/filename.h>

#include <linux/vfs/private/security.h>

#include <linux/vfs/private/nameidata.h>
#include <linux/vfs/private/fs.h>
#include <linux/vfs/fsnotify.h>
#include <linux/vfs/private/namespace.h>

#include "namei/namei-lookup.c"
#include "namei/namei-dots.c"

#define LAST_WORD_IS_DOT 0x2e
#define LAST_WORD_IS_DOTDOT 0x2e2e

extern seqlock_t mount_lock;

static inline const char *hash_name(struct nameidata *nd, const char *name, unsigned long *lastword)
{
    uintptr_t hash = init_name_hash(nd->path.dentry);
    unsigned long len = 0, c, last = 0;

    c = (unsigned char)*name;
    do
    {
        last = (last << 8) + c;
        len++;
        hash = partial_name_hash(c, hash);
        c = (unsigned char)name[len];
    } while (c && c != '/');

    // This is reliable for DOT or DOTDOT, since the component
    // cannot contain NUL characters - top bits being zero means
    // we cannot have had any other pathnames.
    *lastword = last;
    nd->last.hash = end_name_hash(hash);
    nd->last.len = len;

    return name + len;
}

static int norm_d_hash(struct nameidata *nd, const char **name, u64 *hl)
{
    int ret = 0;
    struct dentry *parent = nd->path.dentry;

    nd->state &= ~ND_JUMPED;
    if (unlikely(parent->d_flags & DCACHE_OP_HASH))
    {
        struct qstr this = {{.hash_len = *hl}, .name = *name};

        ret = parent->d_op->d_hash(parent, &this);
        if (ret == 0)
        {
            *hl = this.hash_len;
            *name = this.name;
        }
    }

    return ret;
}

static inline const char *skip_slash(const char *name)
{
    while (*name == '/')
    {
        name++;
    }

    return name;
}

static inline void put_link(struct nameidata *nd)
{
    struct saved *last = nd->stack + --nd->depth;
    do_delayed_call(&last->done);
    if (!(nd->flags & LOOKUP_RCU))
        path_put(&last->link);
}

/*
 * Try to skip to top of mountpoint pile in rcuwalk mode.  Fail if
 * we meet a managed dentry that would need blocking.
 */
static bool __follow_mount_rcu(struct nameidata *nd, struct path *path)
{
    struct dentry *dentry = path->dentry;
    unsigned int flags = dentry->d_flags;
    bool ret;

    if (likely(!(flags & DCACHE_MANAGED_DENTRY)))
    {
        ret = true;
    }
    else if (unlikely(nd->flags & LOOKUP_NO_XDEV))
    {
        ret = false;
    }
    else
    {
        for (;;)
        {
            /*
             * Don't forget we might have a non-mountpoint managed dentry
             * that wants to block transit.
             */
            if (unlikely(flags & DCACHE_MANAGE_TRANSIT))
            {
                int res = dentry->d_op->d_manage(path, true);
                if (res == -EISDIR)
                {
                    ret = true;
                    break;
                }

                flags = dentry->d_flags;
            }

            if (flags & DCACHE_MOUNTED)
            {
                struct mount *mounted = __lookup_mnt(path->mnt, dentry);
                if (mounted)
                {
                    path->mnt = &mounted->mnt;
                    dentry = path->dentry = mounted->mnt.mnt_root;
                    nd->state |= ND_JUMPED;
                    nd->next_seq = read_seqcount_begin(&dentry->d_seq);
                    flags = dentry->d_flags;
                    // makes sure that non-RCU pathwalk could reach
                    // this state.
                    if (read_seqretry(&mount_lock, nd->m_seq))
                    {
                        ret = false;
                        break;
                    }

                    continue;
                }
                if (read_seqretry(&mount_lock, nd->m_seq))
                {
                    ret = false;
                    break;
                }
            }

            ret = !(flags & DCACHE_NEED_AUTOMOUNT);

            break;
        }
    }

    return ret;
}

static int follow_automount(struct path *path, int *count, unsigned lookup_flags)
{
    pr_todo();
    return 0;
}

static int __traverse_mounts(struct path *path, unsigned flags, bool *jumped,
                             int *count, unsigned lookup_flags)
{
    struct vfsmount *mnt = path->mnt;
    bool need_mntput = false;
    int ret = 0;

    while (flags & DCACHE_MANAGED_DENTRY)
    {
        /* Allow the filesystem to manage the transit without i_mutex
         * being held. */
        if (flags & DCACHE_MANAGE_TRANSIT)
        {
            pr_todo();
#if 0
            ret = path->dentry->d_op->d_manage(path, false);
            flags = smp_load_acquire(&path->dentry->d_flags);
            if (ret < 0)
                break;
#endif
        }

        if (flags & DCACHE_MOUNTED)
        { // something's mounted on it..
            struct vfsmount *mounted = lookup_mnt(path);
            if (mounted)
            { // ... in our namespace
                dput(path->dentry);
                if (need_mntput)
                    mntput(path->mnt);
                path->mnt = mounted;
                path->dentry = dget(mounted->mnt_root);
                // here we know it's positive
                flags = path->dentry->d_flags;
                need_mntput = true;
                continue;
            }
        }

        if (!(flags & DCACHE_NEED_AUTOMOUNT))
            break;

        // uncovered automount point
        ret = follow_automount(path, count, lookup_flags);
        flags = d_flags_get_smp(path->dentry);
        if (ret < 0)
            break;
    }

    if (ret == -EISDIR)
        ret = 0;
    // possible if you race with several mount --move
    if (need_mntput && path->mnt == mnt)
        mntput(path->mnt);
    if (!ret && unlikely(d_flags_negative(flags)))
        ret = -ENOENT;
    *jumped = need_mntput;
    return ret;
}

static inline int traverse_mounts(struct path *path, bool *jumped,
                                  int *count, unsigned lookup_flags)
{
    unsigned flags = d_flags_get_smp(path->dentry);

    return __traverse_mounts(path, flags, jumped, count, lookup_flags);
}

static inline int handle_mounts(struct nameidata *nd, struct dentry *dentry,
                                struct path *path)
{
    bool jumped;
    int ret;

    path->mnt = nd->path.mnt;
    path->dentry = dentry;

    if (nd->flags & LOOKUP_RCU)
    {
        unsigned int seq = nd->next_seq;
        if (likely(__follow_mount_rcu(nd, path)))
            return 0;
        // *path and nd->next_seq might've been clobbered
        path->mnt = nd->path.mnt;
        path->dentry = dentry;
        nd->next_seq = seq;
        if (!try_to_unlazy_next(nd, dentry))
            return -ECHILD;
    }
    ret = traverse_mounts(path, &jumped, &nd->total_link_count, nd->flags);
    if (jumped)
    {
        if (unlikely(nd->flags & LOOKUP_NO_XDEV))
            ret = -EXDEV;
        else
            nd->state |= ND_JUMPED;
    }
    if (unlikely(ret))
    {
        dput(path->dentry);
        if (path->mnt != nd->path.mnt)
            mntput(path->mnt);
    }

    return ret;
}

static bool nd_alloc_stack(struct nameidata *nd)
{
    struct saved *p;

    p = kmalloc_array(MAXSYMLINKS, sizeof(struct saved),
                      nd->flags & LOOKUP_RCU ? GFP_ATOMIC : GFP_KERNEL);
    if (unlikely(!p))
        return false;
    memcpy(p, nd->internal, sizeof(nd->internal));
    nd->stack = p;
    return true;
}

/* path_put is needed afterwards regardless of success or failure */
static bool __legitimize_path(struct path *path, unsigned seq, unsigned mseq)
{
    pr_todo();

    return false;
}

static inline bool legitimize_path(struct nameidata *nd,
                                   struct path *path, unsigned seq)
{
    return __legitimize_path(path, seq, nd->m_seq);
}

static int reserve_stack(struct nameidata *nd, struct path *link)
{
    if (unlikely(nd->total_link_count++ >= MAXSYMLINKS))
        return -ELOOP;

    if (likely(nd->depth != EMBEDDED_LEVELS))
        return 0;
    if (likely(nd->stack != nd->internal))
        return 0;
    if (likely(nd_alloc_stack(nd)))
        return 0;

    if (nd->flags & LOOKUP_RCU)
    {
        // we need to grab link before we do unlazy.  And we can't skip
        // unlazy even if we fail to grab the link - cleanup needs it
        bool grabbed_link = legitimize_path(nd, link, nd->next_seq);

        if (!try_to_unlazy(nd) || !grabbed_link)
            return -ECHILD;

        if (nd_alloc_stack(nd))
            return 0;
    }
    return -ENOMEM;
}

static int set_root(struct nameidata *nd)
{
    /*
     * Jumping to the real root in a scoped-lookup is a BUG in namei, but we
     * still have to ensure it doesn't happen because it will cause a breakout
     * from the dirfd.
     */
    if (WARN_ON(nd->flags & LOOKUP_IS_SCOPED))
        return -ENOTRECOVERABLE;

    if (nd->flags & LOOKUP_RCU)
    {
        get_fs_root(nd->filedesc, &nd->root);
    }
    else
    {
        get_fs_root(nd->filedesc, &nd->root);

        nd->state |= ND_ROOT_GRABBED;
    }

    return 0;
}

static int nd_jump_root(struct nameidata *nd)
{
    if (unlikely(nd->flags & LOOKUP_BENEATH))
        return -EXDEV;
    if (unlikely(nd->flags & LOOKUP_NO_XDEV))
    {
        /* Absolute path arguments to path_init() are allowed. */
        if (nd->path.mnt != NULL && nd->path.mnt != nd->root.mnt)
            return -EXDEV;
    }
    if (!nd->root.mnt)
    {
        int error = set_root(nd);
        if (error)
            return error;
    }
    if (nd->flags & LOOKUP_RCU)
    {
        struct dentry *d;
        nd->path = nd->root;
        d = nd->path.dentry;
        nd->inode = d->d_inode;
        nd->seq = nd->root_seq;
        if (read_seqcount_retry(&d->d_seq, nd->seq))
            return -ECHILD;
    }
    else
    {
        path_put(&nd->path);
        nd->path = nd->root;
        path_get(&nd->path);
        nd->inode = nd->path.dentry->d_inode;
    }
    nd->state |= ND_JUMPED;
    return 0;
}

static const char *pick_link(struct nameidata *nd, struct path *link,
                             struct inode *inode, int flags)
{
    struct saved *last;
    const char *res;
    int error = reserve_stack(nd, link);

    if (unlikely(error))
    {
        if (!(nd->flags & LOOKUP_RCU))
            path_put(link);
        return ERR_PTR(error);
    }
    last = nd->stack + nd->depth++;
    last->link = *link;
    clear_delayed_call(&last->done);
    last->seq = nd->next_seq;

    if (flags & WALK_TRAILING)
    {
        error = may_follow_link(nd, inode);
        if (unlikely(error))
            return ERR_PTR(error);
    }

    if (unlikely(nd->flags & LOOKUP_NO_SYMLINKS) ||
        unlikely(link->mnt->mnt_flags & MNT_NOSYMFOLLOW))
        return ERR_PTR(-ELOOP);

    if (!(nd->flags & LOOKUP_RCU))
    {
        touch_atime(&last->link);
        cond_resched();
    }
    else if (atime_needs_update(&last->link, inode))
    {
        if (!try_to_unlazy(nd))
            return ERR_PTR(-ECHILD);
        touch_atime(&last->link);
    }

    error = security_inode_follow_link(link->dentry, inode,
                                       nd->flags & LOOKUP_RCU);
    if (unlikely(error))
        return ERR_PTR(error);

    res = READ_ONCE(inode->i_link);
    if (!res)
    {
        const char *(*get)(struct dentry *, struct inode *,
                           struct delayed_call *);
        get = inode->i_op->get_link;
        if (nd->flags & LOOKUP_RCU)
        {
            res = get(NULL, inode, &last->done);
            if (res == ERR_PTR(-ECHILD) && try_to_unlazy(nd))
                res = get(link->dentry, inode, &last->done);
        }
        else
        {
            res = get(link->dentry, inode, &last->done);
        }
        if (!res)
            goto all_done;
        if (IS_ERR(res))
            return res;
    }
    if (*res == '/')
    {
        error = nd_jump_root(nd);
        if (unlikely(error))
            return ERR_PTR(error);
        while (unlikely(*++res == '/'))
            ;
    }
    if (*res)
        return res;
all_done: // pure jump
    put_link(nd);
    return NULL;
}

const char *step_into(struct nameidata *nd, int flags,
                      struct dentry *dentry)
{
    struct path path;
    struct inode *inode;
    int err;

    err = handle_mounts(nd, dentry, &path);
    if (err < 0)
        return ERR_PTR(err);

    inode = path.dentry->d_inode;
    if (likely(!d_is_symlink(path.dentry)) ||
        ((flags & WALK_TRAILING) && !(nd->flags & LOOKUP_FOLLOW)) ||
        (flags & WALK_NOFOLLOW))
    {
        /* not a symlink or should not follow */
        if (nd->flags & LOOKUP_RCU)
        {
            if (read_seqcount_retry(&path.dentry->d_seq, nd->next_seq))
                return ERR_PTR(-ECHILD);
            if (unlikely(!inode))
                return ERR_PTR(-ENOENT);
        }
        else
        {
            dput(nd->path.dentry);
            if (nd->path.mnt != path.mnt)
                mntput(nd->path.mnt);
        }
        nd->path = path;
        nd->inode = inode;
        nd->seq = nd->next_seq;
        return NULL;
    }
    if (nd->flags & LOOKUP_RCU)
    {
        /* make sure that d_is_symlink above matches inode */
        if (read_seqcount_retry(&path.dentry->d_seq, nd->next_seq))
            return ERR_PTR(-ECHILD);
    }
    else
    {
        if (path.mnt == nd->path.mnt)
            mntget(path.mnt);
    }

    return pick_link(nd, &path, inode, flags);
}

static const char *walk_component(struct nameidata *nd, int flags)
{
    struct dentry *dentry;
    /*
     * "." and ".." are special - ".." especially so because it has
     * to be able to know about the current root directory and
     * parent relationships.
     */
    if (unlikely(nd->last_type != LAST_NORM))
    {
        if (!(flags & WALK_MORE) && nd->depth)
            put_link(nd);
        return handle_dots(nd, nd->last_type);
    }

    dentry = lookup_fast(nd);
    if (IS_ERR(dentry))
        return ERR_CAST(dentry);
    if (unlikely(!dentry))
    {
        dentry = lookup_slow(&nd->last, nd->path.dentry, nd->flags);
        if (IS_ERR(dentry))
            return ERR_CAST(dentry);
    }
    if (!(flags & WALK_MORE) && nd->depth)
        put_link(nd);

    return step_into(nd, flags, dentry);
}

/*
 * Name resolution.
 * This is the basic name resolution function, turning a pathname into
 * the final dentry. We expect 'base' to be positive and a directory.
 *
 * Returns 0 and nd will have valid dentry and mnt on success.
 * Returns error and drops reference to input namei data on failure.
 */
static int link_path_walk(const char *name, struct nameidata *nd)
{
    int depth = 0; // depth <= nd->depth
    int err;

    nd->last_type = LAST_ROOT;
    nd->flags |= LOOKUP_PARENT;
    if (IS_ERR(name))
        return PTR_ERR(name);
    while (*name == '/')
        name++;
    if (!*name)
    {
        nd->dir_mode = 0; // short-circuit the 'hardening' idiocy
        return 0;
    }

    /* At this point we know we have a real path component. */
    for (;;)
    {
        struct mnt_idmap *idmap;
        const char *link;
        unsigned long lastword;

        idmap = mnt_idmap(nd->path.mnt);
        err = may_lookup(idmap, nd);
        if (err)
            return err;

        nd->last.name = name;
        name = hash_name(nd, name, &lastword);

        switch (lastword)
        {
        case LAST_WORD_IS_DOTDOT:
            nd->last_type = LAST_DOTDOT;
            nd->state |= ND_JUMPED;
            break;

        case LAST_WORD_IS_DOT:
            nd->last_type = LAST_DOT;
            break;

        default:
            nd->last_type = LAST_NORM;
            nd->state &= ~ND_JUMPED;

            struct dentry *parent = nd->path.dentry;
            if (unlikely(parent->d_flags & DCACHE_OP_HASH))
            {
                err = parent->d_op->d_hash(parent, &nd->last);
                if (err < 0)
                    return err;
            }
        }

        if (!*name)
            goto OK;
        /*
         * If it wasn't NUL, we know it was '/'. Skip that
         * slash, and continue until no more slashes.
         */
        do
        {
            name++;
        } while (unlikely(*name == '/'));

        if (unlikely(!*name))
        {
        OK:
            /* pathname or trailing symlink, done */
            if (!depth)
            {
                nd->dir_vfsuid = i_uid_into_vfsuid(idmap, nd->inode);
                nd->dir_mode = nd->inode->i_mode;
                nd->flags &= ~LOOKUP_PARENT;
                return 0;
            }
            /* last component of nested symlink */
            name = nd->stack[--depth].name;
            link = walk_component(nd, 0);
        }
        else
        {
            /* not the last component */
            link = walk_component(nd, WALK_MORE);
        }
        if (unlikely(link))
        {
            if (IS_ERR(link))
                return PTR_ERR(link);
            /* a symlink to follow */
            nd->stack[depth++].name = name;
            name = link;
            continue;
        }
        if (unlikely(!d_can_lookup(nd->path.dentry)))
        {
            if (nd->flags & LOOKUP_RCU)
            {
                if (!try_to_unlazy(nd))
                    return -ECHILD;
            }
            return -ENOTDIR;
        }
    }
}

static void restore_nameidata(void)
{
}

static void drop_links(struct nameidata *nd)
{
    int i = nd->depth;
    while (i--)
    {
        struct saved *last = nd->stack + i;
        do_delayed_call(&last->done);
        clear_delayed_call(&last->done);
    }
}

static void leave_rcu(struct nameidata *nd)
{
    nd->flags &= ~LOOKUP_RCU;
    nd->seq = nd->next_seq = 0;
    rcu_read_unlock();
}

static void terminate_walk(struct nameidata *nd)
{
    drop_links(nd);
    if (!(nd->flags & LOOKUP_RCU))
    {
        int i;
        path_put(&nd->path);
        for (i = 0; i < nd->depth; i++)
            path_put(&nd->stack[i].link);
        if (nd->state & ND_ROOT_GRABBED)
        {
            path_put(&nd->root);
            nd->state &= ~ND_ROOT_GRABBED;
        }
    }
    else
    {
        leave_rcu(nd);
    }
    nd->depth = 0;
    nd->path.mnt = NULL;
    nd->path.dentry = NULL;
}

static int complete_walk(struct nameidata *nd)
{
    pr_todo();
    return 0;
}

static inline const char *lookup_last(struct nameidata *nd)
{
    if (nd->last_type == LAST_NORM && nd->last.name[nd->last.len])
        nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

    return walk_component(nd, WALK_TRAILING);
}

static int do_tmpfile(struct nameidata *nd, unsigned flags,
                      const struct open_flags *op,
                      struct file *file)
{
    pr_todo();

    return -1;
}

/*
 must be paired with terminate_walk()
*/
static const char *path_init(struct nameidata *nd, unsigned flags)
{
    int error;
    const char *s = nd->name->name;

    /* LOOKUP_CACHED requires RCU, ask caller to retry */
    if ((flags & (LOOKUP_RCU | LOOKUP_CACHED)) == LOOKUP_CACHED)
        return ERR_PTR(-EAGAIN);

    if (!*s)
        flags &= ~LOOKUP_RCU;
    if (flags & LOOKUP_RCU)
        rcu_read_lock();
    else
        nd->seq = nd->next_seq = 0;

    nd->flags = flags;
    nd->state |= ND_JUMPED;

    if (nd->state & ND_ROOT_PRESET)
    {
        pr_todo();
        return s;
    }

    nd->root.mnt = NULL;

    /* Absolute pathname -- fetch the root (LOOKUP_IN_ROOT uses nd->dfd). */
    if (*s == '/' && !(flags & LOOKUP_IN_ROOT))
    {
        error = nd_jump_root(nd);
        if (unlikely(error))
            return ERR_PTR(error);
        return s;
    }

    return s;
}

static int handle_lookup_down(struct nameidata *nd)
{
    if (!(nd->flags & LOOKUP_RCU))
        dget(nd->path.dentry);

    nd->next_seq = nd->seq;

    return PTR_ERR(step_into(nd, WALK_NOFOLLOW, nd->path.dentry));
}

static int path_lookupat(struct nameidata *nd, unsigned flags, struct path *path)
{
    const char *s;
    int err;

    s = path_init(nd, flags);

    if (unlikely(flags & LOOKUP_DOWN) && !IS_ERR(s))
    {
        err = handle_lookup_down(nd);
        if (unlikely(err < 0))
            s = ERR_PTR(err);
    }

    while (1)
    {
        err = link_path_walk(s, nd);
        if (err)
            break;

        s = lookup_last(nd);
        if (s == NULL)
            break;
    }

    if (!err && unlikely(nd->flags & LOOKUP_MOUNTPOINT))
    {
        err = handle_lookup_down(nd);
        nd->state &= ~ND_JUMPED; // no d_weak_revalidate(), please...
    }

    if (!err)
        err = complete_walk(nd);

    if (!err && nd->flags & LOOKUP_DIRECTORY)
    {
        if (!d_can_lookup(nd->path.dentry))
            err = -ENOTDIR;
    }

    if (!err)
    {
        *path = nd->path;
        nd->path.mnt = NULL;
        nd->path.dentry = NULL;
    }

    terminate_walk(nd);

    return err;
}

static int do_open(struct nameidata *nd,
                   struct file *file, const struct open_flags *op)
{
    struct mnt_idmap *idmap;
    int open_flag = op->open_flag;
    bool do_truncate;
    int acc_mode;
    int error;

    pr_todo();

    if (!(file->f_mode & (FMODE_OPENED | FMODE_CREATED)))
    {
        error = complete_walk(nd);
        if (error)
            return error;
    }

    idmap = mnt_idmap(nd->path.mnt);

    error = op->do_open(&nd->path, file);

    return error;
}

/* Returns 0 and nd will be valid on success; Retuns error, otherwise. */
static int path_parentat(struct nameidata *nd, unsigned flags,
                         struct path *parent)
{
    const char *s;
    int err;

    s = path_init(nd, flags);
    err = link_path_walk(s, nd);

    if (!err)
        err = complete_walk(nd);
    if (!err)
    {
        *parent = nd->path;
        nd->path.mnt = NULL;
        nd->path.dentry = NULL;
    }
    terminate_walk(nd);

    return err;
}

static void __set_nameidata(struct nameidata *p, int dfd, struct filename *name)
{
    struct nameidata *old = NULL; // TODO

    p->stack = p->internal;
    p->depth = 0;
    p->dfd = dfd;
    p->name = name;
    p->path.mnt = NULL;
    p->path.dentry = NULL;
    p->total_link_count = old ? old->total_link_count : 0;
    p->saved = old;
}

static inline void set_nameidata(struct nameidata *p, int dfd, struct filename *name,
                                 const struct path *root)
{
    memset(p, 0, sizeof(struct nameidata));
    __set_nameidata(p, dfd, name);
    p->state = 0;
    if (unlikely(root))
    {
        p->state = ND_ROOT_PRESET;
        p->root = *root;
    }
}

static inline int __filename_lookup(filedesc_t *fdp, int dfd, struct filename *name,
                                    unsigned flags, struct path *path, struct path *root)
{
    int retval;
    struct nameidata nd;

    if (IS_ERR(name))
        return PTR_ERR(name);

    set_nameidata(&nd, dfd, name, root);
    nd.filedesc = fdp;

    retval = path_lookupat(&nd, flags | LOOKUP_RCU, path);
    if (unlikely(retval == -ECHILD))
        retval = path_lookupat(&nd, flags, path);
    if (unlikely(retval == -ESTALE))
        retval = path_lookupat(&nd, flags | LOOKUP_REVAL, path);

    return retval;
}

int user_path_at_empty(int dfd, const char __user *name, unsigned flags,
                       struct path *path, int *empty)
{
    int ret = -1;

    pr_todo();

    return ret;
}

/*
 * Parent directory has inode locked exclusive.  This is one
 * and only case when ->lookup() gets called on non in-lookup
 * dentries - as the matter of fact, this only gets called
 * when directory is guaranteed to have no in-lookup children
 * at all.
 */
struct dentry *lookup_one_qstr_excl(const struct qstr *name,
                                    struct dentry *base,
                                    unsigned int flags)
{
    struct dentry *dentry;
    struct dentry *old;
    struct inode *dir = base->d_inode;

    dentry = lookup_dcache(name, base, flags);
    if (dentry)
        return dentry;

    /* Don't create child dentry for a dead directory. */
    if (unlikely(IS_DEADDIR(dir)))
        return ERR_PTR(-ENOENT);

    dentry = d_alloc(base, name);
    if (unlikely(!dentry))
        return ERR_PTR(-ENOMEM);

    old = dir->i_op->lookup(dir, dentry, flags);
    if (unlikely(old))
    {
        dput(dentry);
        dentry = old;
    }

    return dentry;
}

/****************************************************************/
static int __namei_parentat(struct nameidata *nd,
                            unsigned int flags, struct path *parent,
                            struct qstr *last, int *type)
{
    int retval;

    retval = path_parentat(nd, flags | LOOKUP_RCU, parent);
    if (unlikely(retval == -ECHILD))
        retval = path_parentat(nd, flags, parent);
    if (unlikely(retval == -ESTALE))
        retval = path_parentat(nd, flags | LOOKUP_REVAL, parent);

    if (likely(!retval))
    {
        *last = nd->last;
        *type = nd->last_type;
    }

    return retval;
}

static int __namei_create(struct nameidata *nd,
                          struct path *path, unsigned int lookup_flags,
                          struct dentry **dentry_ret)
{
    struct dentry *dentry = ERR_PTR(-EEXIST);
    struct qstr last;
    bool want_dir = lookup_flags & LOOKUP_DIRECTORY;
    unsigned int reval_flag = lookup_flags & LOOKUP_REVAL;
    unsigned int create_flags = LOOKUP_CREATE | LOOKUP_EXCL;
    int type;
    int err2;
    int error;

    error = __namei_parentat(nd, reval_flag, path, &last, &type);
    if (error)
        return error;

    /*
     * Yucky last component or no last component at all?
     * (foo/., foo/.., /////)
     */
    if (unlikely(type != LAST_NORM))
        goto out;

    /* don't fail immediately if it's r/o, at least try to report other errors */
    err2 = mnt_want_write(path->mnt);
    /*
     * Do the final lookup.  Suppress 'create' if there is a trailing
     * '/', and a directory wasn't requested.
     */
    if (last.name[last.len] && !want_dir)
        create_flags = 0;

    inode_lock_nested(path->dentry->d_inode, I_MUTEX_PARENT);

    dentry = lookup_one_qstr_excl(&last, path->dentry,
                                  reval_flag | create_flags);
    if (IS_ERR(dentry))
        goto unlock;

    error = -EEXIST;
    if (d_is_positive(dentry))
        goto fail;

    /*
     * Special case - lookup gave negative, but... we had foo/bar/
     * From the vfs_mknod() POV we just have a negative dentry -
     * all is fine. Let's be bastards - you had / on the end, you've
     * been asking for (non-existent) directory. -ENOENT for you.
     */
    if (unlikely(!create_flags))
    {
        error = -ENOENT;
        goto fail;
    }
    if (unlikely(err2))
    {
        error = err2;
        goto fail;
    }

    *dentry_ret = dentry;

    return 0;

fail:
    dput(dentry);
unlock:
    inode_unlock(path->dentry->d_inode);
    if (!err2)
        mnt_drop_write(path->mnt);
out:
    path_put(path);

    return error;
}

void namei_init(struct nameiargs *ni, struct filedesc *fdp, const char *namep,
                int atfd, int lookflags)
{
    memset(ni, 0, sizeof(struct nameiargs));

    ni->ni_atfd = atfd;
    ni->ni_fdp = fdp;
    ni->ni_name = namep;
    ni->ni_lookflags = lookflags;
}

int namei_create(struct nameiargs *ni)
{
    struct filename *pathname;
    struct nameidata nd;
    int error;

    error = getname(ni->ni_name, &pathname);
    if (error == 0)
    {
        set_nameidata(&nd, ni->ni_atfd, pathname, NULL);
        nd.filedesc = ni->ni_fdp;

        error = __namei_create(&nd, &ni->ni_ret_parent, ni->ni_lookflags, &ni->ni_ret_dentry);

        putname(pathname);
    }

    return error;
}

static inline umode_t vfs_prepare_mode(struct mnt_idmap *idmap,
                                       const struct inode *dir, umode_t mode,
                                       umode_t mask_perms, umode_t type)
{
    pr_todo();
    return mode;
}

/*
 * Look up and maybe create and open the last component.
 *
 * Must be called with parent locked (exclusive in O_CREAT case).
 *
 * Returns 0 on success, that is, if
 *  the file was successfully atomically created (if necessary) and opened, or
 *  the file was not completely opened at this time, though lookups and
 *  creations were performed.
 * These case are distinguished by presence of FMODE_OPENED on file->f_mode.
 * In the latter case dentry returned in @path might be negative if O_CREAT
 * hadn't been specified.
 *
 * An error code is returned on failure.
 */
static struct dentry *lookup_open(struct nameidata *nd, struct file *file,
                                  const struct open_flags *op,
                                  bool got_write)
{
    struct mnt_idmap *idmap;
    struct dentry *dir = nd->path.dentry;
    struct inode *dir_inode = dir->d_inode;
    int open_flag = op->open_flag;
    struct dentry *dentry;
    int error, create_error = 0;
    umode_t mode = op->mode;
    DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

    if (unlikely(IS_DEADDIR(dir_inode)))
        return ERR_PTR(-ENOENT);

    file->f_mode &= ~FMODE_CREATED;
    dentry = d_lookup(dir, &nd->last);
    for (;;)
    {
        if (!dentry)
        {
            dentry = d_alloc_parallel(dir, &nd->last, &wq);
            if (IS_ERR(dentry))
                return dentry;
        }
        if (d_in_lookup(dentry))
            break;

        error = d_revalidate(dentry, nd->flags);
        if (likely(error > 0))
            break;
        if (error)
            goto out_dput;
        d_invalidate(dentry);
        dput(dentry);
        dentry = NULL;
    }
    if (dentry->d_inode)
    {
        /* Cached positive dentry: will open in f_op->open */
        return dentry;
    }

    /*
     * Checking write permission is tricky, bacuse we don't know if we are
     * going to actually need it: O_CREAT opens should work as long as the
     * file exists.  But checking existence breaks atomicity.  The trick is
     * to check access and if not granted clear O_CREAT from the flags.
     *
     * Another problem is returing the "right" error value (e.g. for an
     * O_EXCL open we want to return EEXIST not EROFS).
     */
    if (unlikely(!got_write))
        open_flag &= ~O_TRUNC;
    idmap = mnt_idmap(nd->path.mnt);
    if (open_flag & O_CREAT)
    {
        if (open_flag & O_EXCL)
            open_flag &= ~O_TRUNC;
        mode = vfs_prepare_mode(idmap, dir->d_inode, mode, mode, mode);
        if (likely(got_write))
            create_error = may_o_create(idmap, &nd->path,
                                        dentry, mode);
        else
            create_error = -EROFS;
    }
    if (create_error)
        open_flag &= ~O_CREAT;

    pr_todo();

    if (d_in_lookup(dentry))
    {
        struct dentry *res = dir_inode->i_op->lookup(dir_inode, dentry,
                                                     nd->flags);
        d_lookup_done(dentry);
        if (unlikely(res))
        {
            if (IS_ERR(res))
            {
                error = PTR_ERR(res);
                goto out_dput;
            }
            dput(dentry);
            dentry = res;
        }
    }

    /* Negative dentry, just create the file */
    if (!dentry->d_inode && (open_flag & O_CREAT))
    {
        file->f_mode |= FMODE_CREATED;
        audit_inode_child(dir_inode, dentry, AUDIT_TYPE_CHILD_CREATE);
        if (!dir_inode->i_op->create)
        {
            error = -EACCES;
            goto out_dput;
        }

        error = dir_inode->i_op->create(idmap, dir_inode, dentry,
                                        mode, open_flag & O_EXCL);
        if (error)
            goto out_dput;
    }
    if (unlikely(create_error) && !dentry->d_inode)
    {
        error = create_error;
        goto out_dput;
    }
    return dentry;

out_dput:
    dput(dentry);
    return ERR_PTR(error);
}

static const char *open_last_lookups(struct nameidata *nd,
                                     struct file *file, const struct open_flags *op)
{
    struct dentry *dir = nd->path.dentry;
    int open_flag = op->open_flag;
    bool got_write = false;
    struct dentry *dentry;
    const char *res;

    nd->flags |= op->intent;

    if (nd->last_type != LAST_NORM)
    {
        if (nd->depth)
            put_link(nd);
        return handle_dots(nd, nd->last_type);
    }

    if (!(open_flag & O_CREAT))
    {
        if (nd->last.name[nd->last.len])
            nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
        /* we _can_ be in RCU mode here */
        dentry = lookup_fast(nd);
        if (IS_ERR(dentry))
            return ERR_CAST(dentry);
        if (likely(dentry))
            goto finish_lookup;

        if (WARN_ON_ONCE(nd->flags & LOOKUP_RCU))
            return ERR_PTR(-ECHILD);
    }
    else
    {
        /* create side of things */
        if (nd->flags & LOOKUP_RCU)
        {
            if (!try_to_unlazy(nd))
                return ERR_PTR(-ECHILD);
        }
        audit_inode(nd->name, dir, AUDIT_INODE_PARENT);
        /* trailing slashes? */
        if (unlikely(nd->last.name[nd->last.len]))
            return ERR_PTR(-EISDIR);
    }

    if (open_flag & (O_CREAT | O_TRUNC | O_WRONLY | O_RDWR))
    {
        got_write = !mnt_want_write(nd->path.mnt);
        /*
         * do _not_ fail yet - we might not need that or fail with
         * a different error; let lookup_open() decide; we'll be
         * dropping this one anyway.
         */
    }
    if (open_flag & O_CREAT)
        inode_lock(dir->d_inode);
    else
        inode_lock_shared(dir->d_inode);
    dentry = lookup_open(nd, file, op, got_write);
    if (!IS_ERR(dentry) && (file->f_mode & FMODE_CREATED))
        fsnotify_create(dir->d_inode, dentry);
    if (open_flag & O_CREAT)
        inode_unlock(dir->d_inode);
    else
        inode_unlock_shared(dir->d_inode);

    if (got_write)
        mnt_drop_write(nd->path.mnt);

    if (IS_ERR(dentry))
        return ERR_CAST(dentry);

    if (file->f_mode & (FMODE_OPENED | FMODE_CREATED))
    {
        dput(nd->path.dentry);
        nd->path.dentry = dentry;
        return NULL;
    }

finish_lookup:
    if (nd->depth)
        put_link(nd);
    res = step_into(nd, WALK_TRAILING, dentry);
    if (unlikely(res))
        nd->flags &= ~(LOOKUP_OPEN | LOOKUP_CREATE | LOOKUP_EXCL);
    return res;
}

static int do_o_path(struct nameidata *nd, unsigned flags, const struct open_flags *op)
{
    struct path path;
    int error;

    error = path_lookupat(nd, flags, &path);
    if (error == 0)
    {
        error = op->do_open(&path, op->file);

        path_put(&path);
    }

    return error;
}

static int __namei_path_openat(struct nameidata *nd,
                               const struct open_flags *op, unsigned flags)
{
    int error;

    if (unlikely(op->open_flag & O_PATH))
    {
        error = do_o_path(nd, flags, op);
    }
    else
    {
        const char *s;

        s = path_init(nd, flags);

        while (1)
        {
            error = link_path_walk(s, nd);
            if (error)
                break;
            s = open_last_lookups(nd, op->file, op);
            if (s == NULL)
                break;
        }

        if (!error)
            error = do_open(nd, op->file, op);

        terminate_walk(nd);
    }

    return error;
}

int namei_open(struct nameiargs *ni, struct open_flags *op)
{
    struct filename *pathname;
    struct nameidata nd;
    int flags = op->lookup_flags;
    int error;

    error = getname(ni->ni_name, &pathname);
    if (error == 0)
    {
        set_nameidata(&nd, ni->ni_atfd, pathname, NULL);
        nd.filedesc = ni->ni_fdp;

        error = __namei_path_openat(&nd, op, flags | LOOKUP_RCU);
        if (unlikely(error == -ECHILD))
            error = __namei_path_openat(&nd, op, flags);
        if (unlikely(error == -ESTALE))
            error = __namei_path_openat(&nd, op, flags | LOOKUP_REVAL);

        putname(pathname);
    }

    return error;
}

int namei_lookup(struct nameiargs *ni)
{
    struct filename *name;
    int error;

    error = getname(ni->ni_name, &name);
    if (error == 0)
    {
        error = __filename_lookup(ni->ni_fdp, ni->ni_atfd, name, ni->ni_lookflags, &ni->ni_ret_parent, NULL);

        putname(name);
    }

    return error;
}

int vfs_path_parent_lookup(struct nameiargs *ni, struct qstr *last, int *type,
                           const struct path *root)
{
    struct filename *pathname;
    struct path *parent = &ni->ni_ret_parent;
    struct nameidata nd;
    int error;
    int flags = ni->ni_lookflags;

    error = getname(ni->ni_name, &pathname);
    if (error == 0)
    {
        set_nameidata(&nd, ni->ni_atfd, pathname, root);
        nd.filedesc = ni->ni_fdp;

        error = path_parentat(&nd, flags | LOOKUP_RCU, parent);
        if (unlikely(error == -ECHILD))
            error = path_parentat(&nd, flags, parent);
        if (unlikely(error == -ESTALE))
            error = path_parentat(&nd, flags | LOOKUP_REVAL, parent);

        if (likely(!error))
        {
            *last = nd.last;
            *type = nd.last_type;
        }
    }

    return error;
}

int kern_path(filedesc_t *fdp, const char *name, unsigned int flags, struct path *path)
{
    struct filename *filename;
    int error;

    error = getname(name, &filename);
    if (error == 0)
    {
        error = __filename_lookup(fdp, AT_FDCWD, filename, flags, path, NULL);

        putname(filename);
    }

    return error;
}

void done_path_create(struct path *path, struct dentry *dentry)
{
	if (!IS_ERR(dentry))
		dput(dentry);
	inode_unlock(path->dentry->d_inode);
	mnt_drop_write(path->mnt);
	path_put(path);
}
