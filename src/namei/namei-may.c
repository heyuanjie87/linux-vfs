#include <linux/vfs/fs.h>
#include <linux/vfs/private/nameidata.h>
#include <linux/vfs/private/namei.h>

int check_sticky(struct mnt_idmap *idmap,
                 struct inode *dir, struct inode *inode)
{
    return 0;
}

int inode_permission(struct mnt_idmap *idmap,
                     struct inode *inode, int mask)
{
    pr_todo();
    return 0;
}

bool may_open_dev(const struct path *path)
{
    return true;
}

int may_follow_link(struct nameidata *nd, const struct inode *inode)
{
    pr_todo();
    return 0;
}

int may_lookup(struct mnt_idmap *idmap, struct nameidata *nd)
{
    pr_todo();
    return 0;
}

int may_delete(struct mnt_idmap *idmap, struct inode *dir,
               struct dentry *victim, bool isdir)
{
    struct inode *inode = d_backing_inode(victim);
    int error;

    if (d_is_negative(victim))
        return -ENOENT;
    BUG_ON(!inode);

    BUG_ON(victim->d_parent->d_inode != dir);

    /* Inode writeback is not safe when the uid or gid are invalid. */
    if (!vfsuid_valid(i_uid_into_vfsuid(idmap, inode)) ||
        !vfsgid_valid(i_gid_into_vfsgid(idmap, inode)))
        return -EOVERFLOW;

    audit_inode_child(dir, victim, AUDIT_TYPE_CHILD_DELETE);

    error = inode_permission(idmap, dir, MAY_WRITE | MAY_EXEC);
    if (error)
        return error;
    if (IS_APPEND(dir))
        return -EPERM;

    if (check_sticky(idmap, dir, inode) || IS_APPEND(inode) ||
        IS_IMMUTABLE(inode) || IS_SWAPFILE(inode) ||
        HAS_UNMAPPED_ID(idmap, inode))
        return -EPERM;
    if (isdir)
    {
        if (!d_is_dir(victim))
            return -ENOTDIR;
        if (IS_ROOT(victim))
            return -EBUSY;
    }
    else if (d_is_dir(victim))
        return -EISDIR;
    if (IS_DEADDIR(dir))
        return -ENOENT;

    return 0;
}

int may_o_create(struct mnt_idmap *idmap,
                 const struct path *dir, struct dentry *dentry,
                 umode_t mode)
{
    return 0;
}
