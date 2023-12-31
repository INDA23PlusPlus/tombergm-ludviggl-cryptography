#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <fuse.h>
#include <sodium.h>
#include <blk.h>
#include <err.h>
#include <mtree.h>
#include "cache.h"
#include "client.h"
#include "fs.h"

static struct options
{
	const char *host;
	const char *root;
	const char *pass;
} options;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] =
{
	OPTION("--host=%s", host),
	OPTION("--root=%s", root),
	OPTION("--pass=%s", pass),
	FUSE_OPT_END
};
#undef OPTION

static void usage(const char *name)
{
    fprintf(stdout,
            "Usage:\n"
            "    %s [options] <mount point>\n"
            "Options:\n"
            "    --host=<addr>      Connect to server at address <addr> (default: 127.0.0.1).\n"
            "    --root=<dir>       Use directory <dir> for local files (default: ./cl_root/).\n"
            "    --pass=<password>  Choose / specify password (default: empty string).\n"
            "    --help             Display this help message.\n"
            "\n",
            name);

    exit(EXIT_SUCCESS);
}

static client_t cl;

static int fs_getattr(const char *path, struct stat *stbuf)
{
    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;

    if (type == FS_DIR)
    {
        fs_dir_t *d = cache_get_blk(cl.dir_cache, id);
        if (d == NULL) return -EIO;

        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_mtim  = d->mod;
        stbuf->st_atim  = d->acc;
    }
    else
    {
        fs_file_t *f = cache_get_blk(cl.dir_cache, id);
        if (f == NULL) return -EIO;

        stbuf->st_mode  = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size  = f->size;
        stbuf->st_mtim  = f->mod;
        stbuf->st_atim  = f->acc;
    }

	return 0;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi)
{
    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_FILE) return -ENOTDIR;

    fs_dir_t *dir = cache_get_blk(cl.dir_cache, id);
    if (dir == 0) return -EIO;

    for (unsigned i = 0; i < DIR_MAX_ENTRIES; i++)
    {
        fs_dir_entry_t *entry = &dir->entries[i];
        if (entry->used)
        {
            filler(buf, entry->name, NULL, 0);
        }
    }

    return 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_DIR) return -EISDIR;

	return 0;
}

static int split_path(const char *path, char *lbuf, char *rbuf)
{
    unsigned len = strlen(path);
    unsigned split_id;
    const char *split = NULL;

    for (unsigned i = len - 1; i >= 0; i--)
    {
        if (path[i] == '/')
        {
            split_id = i + 1;
            split    = path + split_id;
            break;
        }
    }

    if (split)
    {
        memcpy(lbuf, path, split_id);
        lbuf[split_id] = '\0';
        memcpy(rbuf, path + split_id, len - split_id);
        rbuf[len - split_id] = '\0';
        return 1;
    }
    else return 0;
}

static int fs_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_IO) return -EIO;
    if (res != -FSERR_NOT_FOUND) return -EEXIST;

    char lbuf[64];
    char rbuf[64];

    if (!split_path(path, lbuf, rbuf)) return -ENOENT;

    unsigned pid;
    res = fs_find_block(&cl, root, lbuf, &pid, &type);
    if (res == -FSERR_IO) return -EIO;
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (type == FS_FILE) return -EISDIR;

    res = fs_create_dir(&cl, pid, rbuf, &id);
    if (res == -FSERR_IO) return -EIO;
    if (res == -FSERR_LONG_NAME) return -ENAMETOOLONG;
    if (res == -FSERR_OOM || res == -FSERR_FULL_DIR) return -ENOMEM;

    fs_dir_t *dir = cache_get_blk(cl.dir_cache, id);
    if (dir == NULL) return -EIO;

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    dir->acc = ts;
    dir->mod = ts;

    dir = cache_get_blk(cl.dir_cache, pid);
    if (dir == NULL) return -EIO;
    dir->acc = ts;
    dir->mod = ts;

    return 0;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *info)
{
    (void)info;
    (void)mode;

    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_IO) return -EIO;
    if (res != -FSERR_NOT_FOUND) return -EEXIST;

    char lbuf[64];
    char rbuf[64];

    if (!split_path(path, lbuf, rbuf)) return -ENOENT;

    unsigned pid;
    res = fs_find_block(&cl, root, lbuf, &pid, &type);
    if (res == -FSERR_IO) return -EIO;
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (type == FS_FILE) return -EISDIR;

    res = fs_create_file(&cl, pid, rbuf, &id);
    if (res == -FSERR_IO) return -EIO;
    if (res == -FSERR_LONG_NAME) return -ENAMETOOLONG;
    if (res == -FSERR_OOM || res == -FSERR_FULL_DIR) return -ENOMEM;

    fs_file_t *file= cache_get_blk(cl.dir_cache, id);
    if (file == NULL) return -EIO;

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    file->acc = ts;
    file->mod = ts;

    fs_dir_t *dir = cache_get_blk(cl.dir_cache, pid);
    if (dir == NULL) return -EIO;

    dir->acc = ts;
    dir->mod = ts;

    return 0;
}

static int fs_truncate(const char *path, off_t length)
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, file_id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &file_id, &type);
    if (res == -FSERR_IO) return -EIO;
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (type == FS_DIR) return -EISDIR;

    res = fs_truncate_file(&cl, file_id, length);
    if (res == -FSERR_IO) return -EIO;
    if (res == -FSERR_OOM) return -ENOMEM;

    fs_file_t *file= cache_get_blk(cl.dir_cache, file_id);
    if (file == NULL) return -EIO;

    timespec_get(&file->mod, TIME_UTC);
    file->acc = file->mod;

	return 0;
}

static int fs_read(const char *path, char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    size_t bread;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_DIR) return -EISDIR;

    res = fs_read_file(&cl, id, buf, size, offset, &bread);
    if (res == -FSERR_IO) return -EIO;

    fs_file_t *file = cache_get_blk(cl.dir_cache, id);
    if (file == NULL) return -EIO;

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    file->acc = ts;

    unsigned pid = file->parent;
    fs_dir_t *parent = cache_get_blk(cl.dir_cache, pid);
    if (parent == NULL) return -EIO;

    parent->acc = ts;

    return bread; // bon appetit
}

static int fs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    size_t bwrit;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_DIR) return -EISDIR;

    res = fs_write_file(&cl, id, buf, size, offset, &bwrit);
    if (res == -FSERR_IO) return -EIO;

    fs_file_t *file = cache_get_blk(cl.dir_cache, id);
    if (file == NULL) return -EIO;

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    file->acc = ts;
    file->mod = ts;

    unsigned pid = file->parent;

    fs_dir_t *dir = cache_get_blk(cl.dir_cache, pid);
    if (dir == NULL) return -EIO;

    dir->acc = ts;
    dir->mod = ts;

    return bwrit;
}

int fs_utimens(const char *path, const struct timespec specs[2])
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_DIR) return -EISDIR;

    fs_file_t *file = cache_get_blk(cl.dir_cache, id);
    if (file == NULL) return -EIO;

    file->acc = specs[0];
    file->mod = specs[1];

    fs_dir_t *parent = cache_get_blk(cl.dir_cache, id);
    if (parent == NULL) return -EIO;

    parent->acc = specs[0];
    parent->mod = specs[1];

    return 0;
}

static int fs_rmdir(const char *path)
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_FILE) return -ENOTDIR;

    fs_dir_t *dir = cache_get_blk(cl.dir_cache, id);
    if (dir == NULL) return -EIO;
    unsigned pid = dir->parent;

    fs_dir_t *parent = cache_get_blk(cl.dir_cache, pid);
    if (parent == NULL) return -EIO;

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    parent->mod = ts;
    parent->acc = ts;

    res = fs_delete_dir(&cl, id);
    if (res == -FSERR_IO) return -EIO;

    return 0;
}

static int fs_unlink(const char *path)
{
    log("%s, path=%s\n", __func__, path);

    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == -FSERR_NOT_FOUND) return -ENOENT;
    if (res == -FSERR_IO) return -EIO;
    if (type == -FS_DIR) return -EISDIR;

    fs_file_t *file = cache_get_blk(cl.dir_cache, id);
    if (file == NULL) return -EIO;
    unsigned pid = file->parent;

    fs_dir_t *parent = cache_get_blk(cl.dir_cache, pid);
    if (parent == NULL) return -EIO;

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    parent->mod = ts;
    parent->acc = ts;

    res = fs_delete_file(&cl, id);
    if (res == -FSERR_IO) return -EIO;

    return 0;
}

static int fs_flush(const char *path, struct fuse_file_info *info)
{
    log("%s, path=%s\n", __func__, path);

    // just flush everything
    (void)path;
    (void)info;
    unsigned res;

    res = client_flush_all(&cl);
    if (res != 0) return -EIO;

    return 0;
}

static struct fuse_operations fs_ops =
{
    .getattr    = fs_getattr,
    .readdir    = fs_readdir,
    .truncate   = fs_truncate,
    .open       = fs_open,
    .read       = fs_read,
    .write      = fs_write,
    .mkdir      = fs_mkdir,
    .create     = fs_create,
    .utimens    = fs_utimens,
    .rmdir      = fs_rmdir,
    .unlink     = fs_unlink,
    .flush      = fs_flush,
};

int main(int argc, char *argv[])
{
    if (argc == 1) usage(argv[0]);
    if (argc == 2 && strcmp(argv[1], "--help") == 0) usage(argv[0]);

	int			ret	= EXIT_SUCCESS;
	struct fuse_args	args	= FUSE_ARGS_INIT(argc, argv);

	options.host = try_ptr(ENOMEM, strdup, "127.0.0.1");
	options.root = try_ptr(ENOMEM, strdup, "./cl_root/");
	options.pass = try_ptr(ENOMEM, strdup, "");

	try_fn(0, fuse_opt_parse, &args, &options, option_spec, NULL);
	try_fn(ENOMEM, fuse_opt_add_arg, &args, "-s");

	try_fn(0, client_start, &cl, options.host, options.root, options.pass);
	try_fn(0, client_flush_all, &cl);

	log("client started\n");

	ret = fuse_main(args.argc, args.argv, &fs_ops, NULL);

	client_stop(&cl);

	log("client stopped\n");

exit:
	if (ret != EXIT_SUCCESS)
	{
		ret = EXIT_FAILURE;
	}

	fuse_opt_free_args(&args);

	return ret;
}
