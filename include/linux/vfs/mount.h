#pragma once

struct super_block;
struct dentry;
struct file;
struct fs_context;
struct file_system_type;

struct vfsmount
{
	struct dentry *mnt_root;	/* root of the mounted tree */
	struct super_block *mnt_sb; /* pointer to superblock */
	int mnt_flags;
	struct mnt_idmap *mnt_idmap;
};

static inline struct mnt_idmap *mnt_idmap(const struct vfsmount *mnt)
{
	/* Pairs with smp_store_release() in do_idmap_mount(). */
	return mnt->mnt_idmap;
}

/*
 * These are the fs-independent mount-flags: up to 32 flags are supported
 *
 * Usage of these is restricted within the kernel to core mount(2) code and
 * callers of sys_mount() only.  Filesystems should be using the SB_*
 * equivalent instead.
 */
#define MS_RDONLY 1		   /* Mount read-only */
#define MS_NOSUID 2		   /* Ignore suid and sgid bits */
#define MS_NODEV 4		   /* Disallow access to device special files */
#define MS_NOEXEC 8		   /* Disallow program execution */
#define MS_SYNCHRONOUS 16  /* Writes are synced at once */
#define MS_REMOUNT 32	   /* Alter flags of a mounted FS */
#define MS_MANDLOCK 64	   /* Allow mandatory locks on an FS */
#define MS_DIRSYNC 128	   /* Directory modifications are synchronous */
#define MS_NOSYMFOLLOW 256 /* Do not follow symlinks */
#define MS_NOATIME 1024	   /* Do not update access times. */
#define MS_NODIRATIME 2048 /* Do not update directory access times */
#define MS_BIND 4096
#define MS_MOVE 8192
#define MS_REC 16384
#define MS_VERBOSE 32768 /* War is peace. Verbosity is silence. \
				MS_VERBOSE is deprecated. */
#define MS_SILENT 32768
#define MS_POSIXACL (1 << 16)	 /* VFS does not apply the umask */
#define MS_UNBINDABLE (1 << 17)	 /* change to unbindable */
#define MS_PRIVATE (1 << 18)	 /* change to private */
#define MS_SLAVE (1 << 19)		 /* change to slave */
#define MS_SHARED (1 << 20)		 /* change to shared */
#define MS_RELATIME (1 << 21)	 /* Update atime relative to mtime/ctime. */
#define MS_KERNMOUNT (1 << 22)	 /* this is a kern_mount call */
#define MS_I_VERSION (1 << 23)	 /* Update inode I_version field */
#define MS_STRICTATIME (1 << 24) /* Always perform atime updates */
#define MS_LAZYTIME (1 << 25)	 /* Update the on-disk [acm]times lazily */

/* These sb flags are internal to the kernel */
#define MS_SUBMOUNT (1 << 26)
#define MS_NOREMOTELOCK (1 << 27)
#define MS_NOSEC (1 << 28)
#define MS_BORN (1 << 29)
#define MS_ACTIVE (1 << 30)
#define MS_NOUSER (1 << 31)

/*
 * Superblock flags that can be altered by MS_REMOUNT
 */
#define MS_RMT_MASK (MS_RDONLY | MS_SYNCHRONOUS | MS_MANDLOCK | MS_I_VERSION | \
					 MS_LAZYTIME)

/*
 * Old magic mount flag and mask
 */
#define MS_MGC_VAL 0xC0ED0000
#define MS_MGC_MSK 0xffff0000

/*
 * open_tree() flags.
 */
#define OPEN_TREE_CLONE 1			/* Clone the target tree and attach the clone */
#define OPEN_TREE_CLOEXEC O_CLOEXEC /* Close the file on execve() */

/*
 * move_mount() flags.
 */
#define MOVE_MOUNT_F_SYMLINKS 0x00000001   /* Follow symlinks on from path */
#define MOVE_MOUNT_F_AUTOMOUNTS 0x00000002 /* Follow automounts on from path */
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#define MOVE_MOUNT_T_SYMLINKS 0x00000010   /* Follow symlinks on to path */
#define MOVE_MOUNT_T_AUTOMOUNTS 0x00000020 /* Follow automounts on to path */
#define MOVE_MOUNT_T_EMPTY_PATH 0x00000040 /* Empty to path permitted */
#define MOVE_MOUNT_SET_GROUP 0x00000100	   /* Set sharing group instead */
#define MOVE_MOUNT_BENEATH 0x00000200	   /* Mount beneath top mount */
#define MOVE_MOUNT__MASK 0x00000377

/*
 * fsopen() flags.
 */
#define FSOPEN_CLOEXEC 0x00000001

/*
 * fspick() flags.
 */
#define FSPICK_CLOEXEC 0x00000001
#define FSPICK_SYMLINK_NOFOLLOW 0x00000002
#define FSPICK_NO_AUTOMOUNT 0x00000004
#define FSPICK_EMPTY_PATH 0x00000008

/*
 * The type of fsconfig() call made.
 */
enum fsconfig_command
{
	FSCONFIG_SET_FLAG = 0,		  /* Set parameter, supplying no value */
	FSCONFIG_SET_STRING = 1,	  /* Set parameter, supplying a string value */
	FSCONFIG_SET_BINARY = 2,	  /* Set parameter, supplying a binary blob value */
	FSCONFIG_SET_PATH = 3,		  /* Set parameter, supplying an object by path */
	FSCONFIG_SET_PATH_EMPTY = 4,  /* Set parameter, supplying an object by (empty) path */
	FSCONFIG_SET_FD = 5,		  /* Set parameter, supplying an object by fd */
	FSCONFIG_CMD_CREATE = 6,	  /* Create new or reuse existing superblock */
	FSCONFIG_CMD_RECONFIGURE = 7, /* Invoke superblock reconfiguration */
	FSCONFIG_CMD_CREATE_EXCL = 8, /* Create new superblock, fail if reusing existing superblock */
};

/*
 * fsmount() flags.
 */
#define FSMOUNT_CLOEXEC 0x00000001

/*
 * Mount attributes.
 */
#define MOUNT_ATTR_RDONLY 0x00000001	  /* Mount read-only */
#define MOUNT_ATTR_NOSUID 0x00000002	  /* Ignore suid and sgid bits */
#define MOUNT_ATTR_NODEV 0x00000004		  /* Disallow access to device special files */
#define MOUNT_ATTR_NOEXEC 0x00000008	  /* Disallow program execution */
#define MOUNT_ATTR__ATIME 0x00000070	  /* Setting on how atime should be updated */
#define MOUNT_ATTR_RELATIME 0x00000000	  /* - Update atime relative to mtime/ctime. */
#define MOUNT_ATTR_NOATIME 0x00000010	  /* - Do not update access times. */
#define MOUNT_ATTR_STRICTATIME 0x00000020 /* - Always perform atime updates */
#define MOUNT_ATTR_NODIRATIME 0x00000080  /* Do not update directory access times */
#define MOUNT_ATTR_IDMAP 0x00100000		  /* Idmap mount to @userns_fd in struct mount_attr. */
#define MOUNT_ATTR_NOSYMFOLLOW 0x00200000 /* Do not follow symlinks */

/*
 * mount_setattr()
 */
struct mount_attr
{
	u64 attr_set;
	u64 attr_clr;
	u64 propagation;
	u64 userns_fd;
};

/* List of all mount_attr versions. */
#define MOUNT_ATTR_SIZE_VER0 32 /* sizeof first published struct */

/*********************************************************/
#define MNT_NOSUID 0x01
#define MNT_NODEV 0x02
#define MNT_NOEXEC 0x04
#define MNT_NOATIME 0x08
#define MNT_NODIRATIME 0x10
#define MNT_RELATIME 0x20
#define MNT_READONLY 0x40 /* does the user want this to be r/o? */
#define MNT_NOSYMFOLLOW 0x80

#define MNT_SHRINKABLE 0x100
#define MNT_WRITE_HOLD 0x200

#define MNT_SHARED 0x1000	  /* if the vfsmount is a shared mount */
#define MNT_UNBINDABLE 0x2000 /* if the vfsmount is a unbindable mount */
/*
 * MNT_SHARED_MASK is the set of flags that should be cleared when a
 * mount becomes shared.  Currently, this is only the flag that says a
 * mount cannot be bind mounted, since this is how we create a mount
 * that shares events with another mount.  If you add a new MNT_*
 * flag, consider how it interacts with shared mounts.
 */
#define MNT_SHARED_MASK (MNT_UNBINDABLE)
#define MNT_USER_SETTABLE_MASK (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC | MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME | MNT_READONLY | MNT_NOSYMFOLLOW)
#define MNT_ATIME_MASK (MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME)

#define MNT_INTERNAL_FLAGS (MNT_SHARED | MNT_WRITE_HOLD | MNT_INTERNAL | \
							MNT_DOOMED | MNT_SYNC_UMOUNT | MNT_MARKED |  \
							MNT_CURSOR)

#define MNT_INTERNAL 0x4000

#define MNT_LOCK_ATIME 0x040000
#define MNT_LOCK_NOEXEC 0x080000
#define MNT_LOCK_NOSUID 0x100000
#define MNT_LOCK_NODEV 0x200000
#define MNT_LOCK_READONLY 0x400000
#define MNT_LOCKED 0x800000
#define MNT_DOOMED 0x1000000
#define MNT_SYNC_UMOUNT 0x2000000
#define MNT_MARKED 0x4000000
#define MNT_UMOUNT 0x8000000
#define MNT_CURSOR 0x10000000

extern void mntput(struct vfsmount *mnt);
struct vfsmount *mntget(struct vfsmount *mnt);
extern int mnt_want_write(struct vfsmount *mnt);
extern void mnt_drop_write(struct vfsmount *mnt);
int mnt_want_write_file(struct file *file);

extern int vfs_create_mount(struct fs_context *fc, struct vfsmount **mnt);
extern int vfs_kern_mount(struct file_system_type *type,
                          int flags, const char *name,
                          void *data,
                          struct vfsmount **mnt_ret);
