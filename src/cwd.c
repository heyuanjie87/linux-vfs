#include <linux/vfs/fs.h>
#include <linux/vfs/cwd.h>

void get_fs_root(filedesc_t *fdp, struct path *root)
{
    filedesc_path_t pa;

    filedesc_lock(fdp);

    filedesc_root_get(fdp, &pa);
    root->dentry = pa.dentry;
    root->mnt = pa.mnt;

    path_get(root);
    filedesc_unlock(fdp);
}

/*
 * Replace the fs->{rootmnt,root} with {mnt,dentry}. Put the old values.
 * It can block.
 */
void set_fs_root(filedesc_t *fdp, const struct path *path)
{
    struct path old_root;
    filedesc_path_t pa;

    path_get(path);

    filedesc_lock(fdp);

    filedesc_root_get(fdp, &pa);
    old_root.dentry = pa.dentry;
    old_root.mnt = pa.mnt;

    pa.dentry = path->dentry;
    pa.mnt = path->mnt;
    filedesc_root_set(fdp, pa);

    filedesc_unlock(fdp);

    if (old_root.dentry)
        path_put(&old_root);
}

/*
 * Replace the fs->{pwdmnt,pwd} with {mnt,dentry}. Put the old values.
 * It can block.
 */
void set_fs_pwd(filedesc_t *fdp, const struct path *path)
{
    struct path old_pwd;
    filedesc_path_t pa;

    path_get(path);

    filedesc_lock(fdp);
    filedesc_pwd_get(fdp, &pa);
    old_pwd.dentry = pa.dentry;
    old_pwd.mnt = pa.mnt;

    pa.dentry = path_dentry(path);
    pa.mnt = path_mnt(path);

    filedesc_pwd_set(fdp, pa);

    filedesc_unlock(fdp);

    if (old_pwd.dentry)
        path_put(&old_pwd);
}
