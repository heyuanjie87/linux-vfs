#include <linux/vfs/private/fs.h>

/*
 * vfsmount lock must be held for write
 */
void mnt_set_mountpoint(struct mount *mnt,
                        struct mountpoint *mp,
                        struct mount *child_mnt)
{
    child_mnt->mnt_mountpoint = mp->m_dentry;
    child_mnt->mnt_parent = mnt;
    child_mnt->mnt_mp = mp;
    hlist_add_head(&child_mnt->mnt_mp_list, &mp->m_list);
}
