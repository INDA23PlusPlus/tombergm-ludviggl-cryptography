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

#define BLK_MAX	16

static client_t cl;

static int lookup_blk(const char *path, blk_id_t *blk_id)
{
	while (path[0] == '/')
	{
		path++;
	}

	if (path[0] == '0' && path[1] != '\0')
	{
		return -1;
	}

	int n;
	if (sscanf(path, "%" SCNu64 "%n", blk_id, &n) != 1)
	{
		return -1;
	}

	if (*blk_id >= BLK_MAX)
	{
		return -1;
	}

	if (path[n] != '\0')
	{
		return -1;
	}

	return 0;
}

static int fs_getattr(const char *path, struct stat *stbuf)
{
	int		res	= 0;
	blk_id_t	blk_id;

	memset(stbuf, 0, sizeof(struct stat));

	if (strcmp(path, "/") == 0)
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
	else if (lookup_blk(path, &blk_id) == 0)
	{
		stbuf->st_mode = S_IFREG | 0644;
		stbuf->st_nlink = 1;
		stbuf->st_size = BLK_DATA_LEN;
	}
	else
	{
		res = -ENOENT;
	}

	return res;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi)
{
	if (strcmp(path, "/") != 0)
	{
		return -ENOENT;
	}

	filler(buf, "." , NULL, 0);
	filler(buf, "..", NULL, 0);

	for (int i = 0; i < BLK_MAX; i++)
	{
		char fname[32];

		sprintf(fname, "%i", i);

		filler(buf, fname , NULL, 0);
	}

	return 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
	blk_id_t	blk_id;

	if (lookup_blk(path, &blk_id) != 0)
	{
		return -ENOENT;
	}

	return 0;
}

static int fs_truncate(const char *path, off_t length)
{
	blk_id_t	blk_id;

	if (lookup_blk(path, &blk_id) != 0)
	{
		return -ENOENT;
	}

	size_t size;

	if (length < BLK_DATA_LEN)
	{
		size = BLK_DATA_LEN - length;
	}
	else
	{
		size = BLK_DATA_LEN;
	}

	if (size != 0)
	{
		char *blk;

		if (size == BLK_DATA_LEN)
		{
			blk = cache_claim_blk(cl.reg_cache, blk_id);
		}
		else
		{
			blk = cache_get_blk(cl.reg_cache, blk_id);
		}

		if (blk == NULL)
		{
			return -EIO;
		}

		memset(&blk[length], 0, size);

		cache_dirty_blk(cl.reg_cache, blk_id);
	}

	return 0;
}

static int fs_read(const char *path, char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	blk_id_t	blk_id;

	if (lookup_blk(path, &blk_id) != 0)
	{
		return -ENOENT;
	}

	if (offset < BLK_DATA_LEN)
	{
		if (offset + size > BLK_DATA_LEN)
		{
			size = BLK_DATA_LEN - offset;
		}
	}
	else
	{
		size = 0;
	}

	if (size != 0)
	{
		char *blk = cache_get_blk(cl.reg_cache, blk_id);

		if (blk == NULL)
		{
			return -EIO;
		}

		memcpy(buf, &blk[offset], size);
	}

	return size;
}

static int fs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	blk_id_t	blk_id;

	if (lookup_blk(path, &blk_id) != 0)
	{
		return -ENOENT;
	}

	if (offset < BLK_DATA_LEN)
	{
		if (offset + size > BLK_DATA_LEN)
		{
			size = BLK_DATA_LEN - offset;
		}
	}
	else
	{
		size = 0;
	}

	if (size != 0)
	{
		char *blk;

		if (size == BLK_DATA_LEN)
		{
			blk = cache_claim_blk(cl.reg_cache, blk_id);
		}
		else
		{
			blk = cache_get_blk(cl.reg_cache, blk_id);
		}

		if (blk == NULL)
		{
			return -EIO;
		}

		memcpy(&blk[offset], buf, size);
	}

	return size;
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
