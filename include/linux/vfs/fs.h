#pragma once

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/wait.h>
#include <linux/kernel.h>
#include <linux/rcupdate.h>
#include <linux/kmalloc.h>
#include <linux/bug.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/bits.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/user_namespace.h>
#include <linux/filedesc.h>
#include <linux/sched.h>
#include <linux/delayed_call.h>
#include <linux/initcall.h>
#include <linux/export.h>
#include <linux/uidgid.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <uapi/linux/magic.h>
#include <uapi/linux/fs.h>
#include <linux/fs_types.h>
#include <linux/hlist.h>
#include <linux/rwsem.h>
#include <linux/pagemap.h>

#include <linux/vfs/audit.h>
#include <linux/vfs/limits.h>
#include <linux/vfs/statfs.h>
#include <linux/vfs/inode.h>
#include <linux/vfs/dentry.h>
#include <linux/vfs/path.h>
#include <linux/vfs/mount.h>
#include <linux/vfs/file.h>
#include <linux/vfs/fcntl.h>
#include <linux/vfs/stat.h>
#include <linux/vfs/mnt_idmapping.h>
#include <linux/vfs/libfs.h>
#include <linux/vfs/superblock.h>
#include <linux/vfs/fs_context.h>
#include <linux/vfs/openat2.h>
#include <linux/vfs/filelock.h>
#include <linux/vfs/attr.h>
#include <linux/vfs/namei.h>
#include <linux/vfs/as.h>
#include <linux/vfs/writeback.h>
#include <linux/vfs/seq_file.h>
#include <linux/vfs/exportfs.h>

#define RENAME_NOREPLACE (1 << 0) /* Don't overwrite target */
#define RENAME_EXCHANGE (1 << 1)  /* Exchange source and dest */
#define RENAME_WHITEOUT (1 << 2)  /* Whiteout source */

#define FS_REQUIRES_DEV 1
#define FS_HAS_SUBTYPE 4
#define FS_USERNS_MOUNT 8 /* Can be mounted by userns root */
#define FS_ALLOW_IDMAP 32 /* FS has been updated to handle vfs idmappings. */
struct file_system_type
{
	void *owner;
	const char *name;
	int fs_flags;

	struct dentry *(*mount)(struct file_system_type *, int,
							const char *, void *);
	void (*kill_sb)(struct super_block *);
	int (*init_fs_context)(struct fs_context *);
	struct file_system_type *next;
	const struct fs_parameter_spec *parameters;
};

static inline vfsuid_t i_uid_into_vfsuid(struct mnt_idmap *idmap,
										 const struct inode *inode)
{
	vfsuid_t v = {0};

	return v;
}

extern int register_filesystem(struct file_system_type *);
extern int unregister_filesystem(struct file_system_type *);

#define ACC_MODE(x) ("\004\002\006\006"[(x) & O_ACCMODE])
#define __FMODE_NONOTIFY ((__force int)FMODE_NONOTIFY)

#define MAY_EXEC 0x00000001
#define MAY_WRITE 0x00000002
#define MAY_READ 0x00000004
#define MAY_APPEND 0x00000008
#define MAY_ACCESS 0x00000010
#define MAY_OPEN 0x00000020
#define MAY_CHDIR 0x00000040
/* called from RCU mode, don't block */
#define MAY_NOT_BLOCK 0x00000080

extern int getname(const char __user *name, struct filename **res);

extern struct file_system_type *get_fs_type(const char *name);
extern void iput(struct inode *);

#define __IS_FLG(inode, flg) ((inode)->i_sb->s_flags & (flg))

#define IS_APPEND(inode) ((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode) ((inode)->i_flags & S_IMMUTABLE)
#define IS_DIRSYNC(inode) (__IS_FLG(inode, SB_SYNCHRONOUS | SB_DIRSYNC) || \
						   ((inode)->i_flags & (S_SYNC | S_DIRSYNC)))
#define IS_SYNC(inode) (__IS_FLG(inode, SB_SYNCHRONOUS) || \
						((inode)->i_flags & S_SYNC))

static inline bool HAS_UNMAPPED_ID(struct mnt_idmap *idmap,
								   struct inode *inode)
{
	pr_todo();
	return false;
}

#define S_KERNEL_FILE (1 << 17) /* File is in use by the kernel (eg. fs/cachefiles) */

static inline ino_t parent_ino(struct dentry *dentry)
{
	ino_t res;

	/*
	 * Don't strictly need d_lock here? If the parent ino could change
	 * then surely we'd have a deeper race in the caller?
	 */
	d_lock(dentry);
	res = dentry->d_parent->d_inode->i_ino;
	d_unlock(dentry);

	return res;
}

struct renamedata
{
	struct mnt_idmap *old_mnt_idmap;
	struct inode *old_dir;
	struct dentry *old_dentry;
	struct mnt_idmap *new_mnt_idmap;
	struct inode *new_dir;
	struct dentry *new_dentry;
	struct inode **delegated_inode;
	unsigned int flags;
};

enum file_time_flags
{
	S_ATIME = 1,
	S_MTIME = 2,
	S_CTIME = 4,
	S_VERSION = 8,
};

static inline bool sb_rdonly(const struct super_block *sb) { return sb->s_flags & SB_RDONLY; }

/*
 * This must be used for allocating filesystems specific inodes to set
 * up the inode reclaim context correctly.
 */
#define alloc_inode_sb(_sb, _cache, _gfp) kmem_cache_alloc(_cache, _gfp)


#define FITRIM		_IOWR('X', 121, struct fstrim_range)	/* Trim */

int rw_verify_area(int read_write, struct file *file, const loff_t *ppos, size_t count);

extern bool atime_needs_update(const struct path *, struct inode *);
extern void touch_atime(const struct path *);
