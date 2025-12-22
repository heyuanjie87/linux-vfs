#include <linux/vfs/fs.h>
#include <linux/vfs/private/nameidata.h>
#include <linux/vfs/private/namei.h>
#include <linux/vfs/private/mount.h>
#include <linux/vfs/private/dcache.h>

extern const char *step_into(struct nameidata *nd, int flags,
                             struct dentry *dentry);

/**
 * path_connected - Verify that a dentry is below mnt.mnt_root
 * @mnt: The mountpoint to check.
 * @dentry: The dentry to check.
 *
 * Rename can sometimes move a file or directory outside of a bind
 * mount, path_connected allows those cases to be detected.
 */
static bool path_connected(struct vfsmount *mnt, struct dentry *dentry)
{
    struct super_block *sb = mnt->mnt_sb;

    /* Bind mounts can have disconnected paths */
    if (mnt->mnt_root == sb->s_root)
        return true;

    return is_subdir(dentry, mnt->mnt_root);
}

static bool choose_mountpoint_rcu(struct mount *m, const struct path *root,
                                  struct path *path, unsigned *seqp)
{
    bool ret = false;

    while (mnt_has_parent(m))
    {
        struct dentry *mountpoint = m->mnt_mountpoint;

        m = m->mnt_parent;
        if (unlikely(root->dentry == mountpoint &&
                     root->mnt == &m->mnt))
            break;

        if (mountpoint != m->mnt.mnt_root)
        {
            path->mnt = &m->mnt;
            path->dentry = mountpoint;
            *seqp = read_seqcount_begin(&mountpoint->d_seq);
            ret = true;
            break;
        }
    }

    return ret;
}

static bool choose_mountpoint(struct mount *m, const struct path *root,
                              struct path *path)
{
    bool found = true;

    pr_todo();

    rcu_read_lock();
    {
        unsigned seq;

        found = choose_mountpoint_rcu(m, root, path, &seq);
    }
    rcu_read_unlock();

    return found;
}

static struct dentry *follow_dotdot(struct nameidata *nd)
{
    struct dentry *parent;

    if (path_equal(&nd->path, &nd->root))
        goto in_root;

    if (unlikely(nd->path.dentry == nd->path.mnt->mnt_root))
    {
        struct path path;

        if (!choose_mountpoint(real_mount(nd->path.mnt),
                               &nd->root, &path))
            goto in_root;
        path_put(&nd->path);
        nd->path = path;
        nd->inode = path.dentry->d_inode;
        if (unlikely(nd->flags & LOOKUP_NO_XDEV))
            return ERR_PTR(-EXDEV);
    }
    /* rare case of legitimate dget_parent()... */
    parent = dget_parent(nd->path.dentry);
    if (unlikely(!path_connected(nd->path.mnt, parent)))
    {
        dput(parent);
        return ERR_PTR(-ENOENT);
    }
    return parent;

in_root:
    if (unlikely(nd->flags & LOOKUP_BENEATH))
        return ERR_PTR(-EXDEV);
    return dget(nd->path.dentry);
}

static const char *handle_dots(struct nameidata *nd, int type)
{
    if (type == LAST_DOTDOT)
    {
        struct dentry *parent;
        const char *error = NULL;

        parent = follow_dotdot(nd);

		if (IS_ERR(parent))
			return ERR_CAST(parent);
		error = step_into(nd, WALK_NOFOLLOW, parent);
		if (unlikely(error))
			return error;

        pr_todo();
    }

    return NULL;
}
