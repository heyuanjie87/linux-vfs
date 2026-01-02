#include <linux/vfs/private/fs.h>

int fc_mount(struct fs_context *fc, struct vfsmount **mnt)
{
    int err = vfs_get_tree(fc);

    *mnt = NULL;
    if (!err)
    {
        up_write(&fc->root->d_sb->s_umount);
        err = vfs_create_mount(fc, mnt);
    }

    return err;
}

int vfs_kern_mount(struct file_system_type *type,
                   int flags, const char *name,
                   void *data,
                   struct vfsmount **mnt_ret)
{
    struct fs_context *fc;
    int ret = 0;

    if (!type)
        return -EINVAL;

    fc = fs_context_for_mount(type, flags);
    if (IS_ERR(fc))
        return PTR_ERR(fc);

    if (name)
        ret = vfs_parse_fs_string(fc, "source",
                                  name, strlen(name));
    if (!ret)
        ret = parse_monolithic_mount_data(fc, data);
    if (!ret)
        ret = fc_mount(fc, mnt_ret);

    put_fs_context(fc);

    return ret;
}
