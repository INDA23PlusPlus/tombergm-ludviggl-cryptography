#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <fuse.h>
#include <sodium.h>
#include <blk.h>
#include "cache.h"
#include "client.h"
#include "fs.h"

#define BLK_MAX	16

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
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else
    {
        fs_file_t *f = cache_get_blk(cl.dir_cache, id);
        if (f == NULL) return -EIO;

        stbuf->st_mode  = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size  = f->size;
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

    for (unsigned i = 0; i < dir->entry_count; i++)
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
    unsigned root, id, type;
    int res;

    root = fs_get_root(&cl);
    if (root == 0) return -EIO;

    res = fs_find_block(&cl, root, path, &id, &type);
    if (res == FSERR_NOT_FOUND) return -ENOENT;
    if (res == FSERR_IO) return -EIO;
    if (type == FS_DIR) return -EISDIR;

	return 0;
}

static int fs_truncate(const char *path, off_t length)
{
	return 0;
}

static int fs_read(const char *path, char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
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

    return bread; // bon appetit
}

static int fs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
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

    return bwrit;
}

static struct fuse_operations fs_ops =
{
	.getattr	= fs_getattr,
	.readdir	= fs_readdir,
	.truncate	= fs_truncate,
	.open		= fs_open,
	.read		= fs_read,
	.write		= fs_write,
};

int main(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;

	ret = client_start(&cl, "./cl_root/", "password123");
	if (ret != 0)
	{
		ret = EXIT_FAILURE;
		goto exit;
	}

	ret = fuse_main(argc, argv, &fs_ops, NULL);

	client_stop(&cl);

exit:

	return ret;
}
