#include <linux/vfs/private/fs.h>

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

static inline void mnt_add_instance(struct mount *m, struct super_block *s)
{
    list_add_tail(&m->mnt_instance, &s->s_mounts);
}

static void setup_mnt(struct mount *m, struct dentry *root)
{
	struct super_block *s = root->d_sb;

	atomic_inc(&s->s_active);
	m->mnt.mnt_sb = s;
	m->mnt.mnt_root = dget(root);
	m->mnt_mountpoint = m->mnt.mnt_root;
	m->mnt_parent = m;

	mnt_add_instance(m, s);
}

int vfs_create_mount(struct fs_context *fc, struct vfsmount **m)
{
    struct mount *mnt;

    if (!fc->root)
        return -EINVAL;

    mnt = alloc_vfsmnt(fc->source ?: "none");
    if (!mnt)
        return -ENOMEM;

    if (fc->sb_flags & SB_KERNMOUNT)
        mnt->mnt.mnt_flags = MNT_INTERNAL;

    setup_mnt(mnt, fc->root);

    *m = &mnt->mnt;

    return 0;
}
