#pragma once

#include <linux/list_bl.h>
#include <linux/stringhash.h>

struct qstr;
struct dentry;

#define DCACHE_CANT_MOUNT		0x00000100

struct dcache {
    struct hlist_bl_head *hashtable;
    unsigned long hash_shift;
};

void dont_mount(struct dentry *dentry);
struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name);
extern void shrink_dcache_parent(struct dentry *);
void d_move(struct dentry *dentry, struct dentry *target);

extern struct dentry *d_find_alias(struct inode *);
