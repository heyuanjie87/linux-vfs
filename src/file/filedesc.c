#include <linux/filedesc.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#define FD_USED ((void *)1)

int filedesc_init(filedesc_t *fdp, bool is_user)
{
    memset(fdp, 0, sizeof(*fdp));
    INIT_LIST_HEAD(&fdp->fdt_head);
    fdp->fdt_cur = &fdp->fdt_default;

    fdp->fdt_default.max_fds = NR_OPEN_DEFAULT;
    fdp->fdt_default.files = fdp->file_default;
    INIT_LIST_HEAD(&fdp->fdt_default.node);
    fdp->is_user = is_user;
    spin_lock_init(&fdp->file_lock);

    return 0;
}

int filedesc_slot_alloc(filedesc_t *fdp,  struct filedesc_slot *slot)
{
    struct fdtable *fdt = fdp->fdt_cur;

    for (int i = 0; i < fdt->max_fds; i ++)
    {
        if (!fdt->files[i])
        {
            slot->result_fd = i;
            slot->fdt = fdt;
            fdt->files[i] = FD_USED;
            break;
        }
    }

    return 0;
}

void filedesc_slot_free(struct filedesc *fdp, struct filedesc_slot *slot)
{

}

int filedesc_slot_finstall(struct filedesc *fdp, struct filedesc_slot *slot, void *file)
{
    slot->fdt->files[slot->result_fd] = file;

    return 0;
}

void filedesc_root_set(filedesc_t *fdp, filedesc_path_t root)
{
    fdp->root = root;
}

void filedesc_root_get(filedesc_t *fdp, filedesc_path_t *root)
{
    *root = fdp->root;
}

void* filedesc_file_get(filedesc_t *fdp, unsigned int fd, bool isclose)
{
    void *file;
    struct fdtable *fdt;

    fdt = fdp->fdt_cur;

    file = fdt->files[fd];
    if (file == FD_USED)
    {
        file = NULL;
    }
    else if (file && isclose)
    {
        fdt->files[fd] = NULL;
    }

    return file;
}

void filedesc_pwd_set(filedesc_t *fdp, filedesc_path_t path)
{
    fdp->pwd = path;
}

void filedesc_pwd_get(filedesc_t *fdp, filedesc_path_t *path)
{
    *path = fdp->pwd;
}

void filedesc_lock(filedesc_t *fdp)
{

}

void filedesc_unlock(filedesc_t *fdp)
{

}
