#pragma once

#include <linux/list.h>
#include <linux/vfs/mount.h>

struct mountpoint;

struct mnt_namespace
{
};

struct mount
{
    struct hlist_node mnt_hash;
    struct vfsmount mnt;
    struct mount *mnt_parent;
    struct dentry *mnt_mountpoint;
    struct list_head mnt_mounts;   /* list of children, anchored here */
    struct list_head mnt_child;    /* and going through their mnt_child */
    struct list_head mnt_instance; /* mount instance on sb->s_mounts */
    struct mnt_namespace *mnt_ns;  /* containing namespace */
    struct mountpoint *mnt_mp;	/* where is it mounted */
    union
    {
        struct hlist_node mnt_mp_list; /* list mounts with the same mountpoint */
        struct hlist_node mnt_umount;
    };
    int mnt_count;
};

struct mountpoint
{
    struct hlist_node m_hash;
    struct dentry *m_dentry;
    struct hlist_head m_list;
    int m_count;
};

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL) /* distinct from any mnt_namespace */

static inline struct mount *real_mount(struct vfsmount *mnt)
{
    return container_of(mnt, struct mount, mnt);
}

bool is_local_mountpoint(struct dentry *dentry);
void detach_mounts(struct dentry *dentry);

int path_umount(struct path *path, int flags);

static inline int mnt_has_parent(struct mount *mnt)
{
    return mnt != mnt->mnt_parent;
}

static inline int is_mounted(struct vfsmount *mnt)
{
    /* neither detached nor internal? */
    return !IS_ERR_OR_NULL(real_mount(mnt)->mnt_ns);
}

extern int fc_mount(struct fs_context *fc, struct vfsmount **mnt);
