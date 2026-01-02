#pragma once

#include <linux/types.h>
#include <linux/spinlock_types.h>

#define NR_OPEN_DEFAULT 32

struct fdtable
{
    struct list_head node;
    void **files;
    unsigned int max_fds;
};

struct filedesc_slot
{
    struct fdtable *fdt;
    int result_fd;
};

struct filedesc_path
{
    void *mnt;
    void *dentry;
};
typedef struct filedesc_path filedesc_path_t;

struct filedesc
{
    struct list_head fdt_head;
    struct fdtable *fdt_cur;

    filedesc_path_t pwd;
    filedesc_path_t root;

    spinlock_t file_lock;
    struct fdtable fdt_default;
    void *file_default[NR_OPEN_DEFAULT];
    bool is_user;
};

typedef struct filedesc filedesc_t;

int filedesc_init(filedesc_t *fdp, bool is_user);
int filedesc_slot_alloc(filedesc_t *fdp, struct filedesc_slot *slot);
void filedesc_slot_free(struct filedesc *fdp, struct filedesc_slot *slot);
int filedesc_slot_finstall(struct filedesc *fdp, struct filedesc_slot *slot, void *file);

void filedesc_root_set(filedesc_t *fdp, filedesc_path_t root);
void filedesc_root_get(filedesc_t *fdp, filedesc_path_t *root);

void *filedesc_file_get(filedesc_t *fdp, unsigned int fd, bool isclose);

void filedesc_pwd_set(filedesc_t *fdp, filedesc_path_t root);
void filedesc_pwd_get(filedesc_t *fdp, filedesc_path_t *root);

void filedesc_lock(filedesc_t *fdp);
void filedesc_unlock(filedesc_t *fdp);
