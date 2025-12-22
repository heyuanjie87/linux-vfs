#include <linux/vfs/fs.h>
#include <linux/vfs/private/filename.h>
#include <linux/vfs/private/namei.h>

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 * PATH_MAX includes the nul terminator --RR.
 */

#define EMBEDDED_NAME_MAX (PATH_MAX - offsetof(struct filename, iname))

static inline void initname(struct filename *name, const char __user *uptr)
{
    name->uptr = uptr;
    name->aname = NULL;
    atomic_set(&name->refcnt, 1);
}

static struct filename *__getname(void)
{
    return kmalloc(PATH_MAX, GFP_KERNEL);
}

static inline void __putname(void *n)
{
    kfree(n);
}

int getname_flags(const char __user *filename, int flags, struct filename **res)
{
    struct filename *result;
    char *kname;
    int len;

    result = __getname();
    if (unlikely(!result))
        return -ENOMEM;

    /*
     * First, try to embed the struct filename inside the names_cache
     * allocation
     */
    kname = (char *)result->iname;
    result->name = kname;

    len = strncpy_from_user(kname, filename, EMBEDDED_NAME_MAX);
    /*
     * Handle both empty path and copy failure in one go.
     */
    if (unlikely(len <= 0))
    {
        if (unlikely(len < 0))
        {
            __putname(result);
            return len;
        }

        /* The empty path is special. */
        if (!(flags & LOOKUP_EMPTY))
        {
            __putname(result);
            return -ENOENT;
        }
    }

    /*
     * Uh-oh. We have a name that's approaching PATH_MAX. Allocate a
     * separate struct filename so we can dedicate the entire
     * names_cache allocation for the pathname, and re-do the copy from
     * userland.
     */
    if (unlikely(len == EMBEDDED_NAME_MAX))
    {
        const size_t size = offsetof(struct filename, iname[1]);
        kname = (char *)result;

        /*
         * size is chosen that way we to guarantee that
         * result->iname[0] is within the same object and that
         * kname can't be equal to result->iname, no matter what.
         */
        result = kzalloc(size, GFP_KERNEL);
        if (unlikely(!result))
        {
            __putname(kname);
            return -ENOMEM;
        }
        result->name = kname;
        len = strncpy_from_user(kname, filename, PATH_MAX);
        if (unlikely(len < 0))
        {
            __putname(kname);
            kfree(result);
            return len;
        }
        /* The empty path is special. */
        if (unlikely(!len) && !(flags & LOOKUP_EMPTY))
        {
            __putname(kname);
            kfree(result);
            return -ENOENT;
        }
        if (unlikely(len == PATH_MAX))
        {
            __putname(kname);
            kfree(result);
            return -ENAMETOOLONG;
        }
    }
    initname(result, filename);

    *res = result;

    return 0;
}

int getname(const char __user *name, struct filename **res)
{
    return getname_flags(name, 0, res);
}

void putname(struct filename *name)
{
    int refcnt;

    if (IS_ERR_OR_NULL(name))
        return;

    refcnt = atomic_read(&name->refcnt);
    if (refcnt != 1)
    {
        if (WARN_ON_ONCE(!refcnt))
            return;

        if (!atomic_dec_and_test(&name->refcnt))
            return;
    }

    if (name->name != name->iname)
    {
        __putname((void*)name->name);
        kfree(name);
    }
    else
        __putname(name);
}
