#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <fuse.h>
#include <sodium.h>
#include <blk.h>
#include "client.h"

static client_t cl;

static const char *hello_path = "/hello";

static int fs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));

	if (strcmp(path, "/") == 0)
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
	else if (strcmp(path, hello_path) == 0)
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

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, hello_path + 1, NULL, 0);

	return 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path, hello_path) != 0)
	{
		return -ENOENT;
	}

	return 0;
}

static int fs_truncate(const char *path, off_t length)
{
	if (strcmp(path, hello_path) != 0)
	{
		return -ENOENT;
	}

	if (length < BLK_DATA_LEN)
	{
		size_t size = BLK_DATA_LEN - length;

		blk_t blk;

		if (length != 0)
		{
			if (client_rd_blk(&cl, &blk, 0) != 0)
			{
				return -EIO;
			}
		}

		memset(&blk.data[length], 0, size);

		if (client_wr_blk(&cl, &blk, 0) != 0)
		{
			return -EIO;
		}
	}

	return 0;
}

static int fs_read(const char *path, char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	if (strcmp(path, hello_path) != 0)
	{
		return -ENOENT;
	}

	if (offset < BLK_DATA_LEN)
	{
		if (offset + size > BLK_DATA_LEN)
		{
			size = BLK_DATA_LEN - offset;
		}

		blk_t blk;
		if (client_rd_blk(&cl, &blk, 0) != 0)
		{
			return -EIO;
		}

		memcpy(buf, &blk.data[offset], size);
	}
	else
	{
		size = 0;
	}

	return size;
}

static int fs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	if (strcmp(path, hello_path) != 0)
	{
		return -ENOENT;
	}

	if (offset < BLK_DATA_LEN)
	{
		if (offset + size > BLK_DATA_LEN)
		{
			size = BLK_DATA_LEN - offset;
		}

		blk_t blk;

		if (offset == 0 && size == BLK_DATA_LEN)
		{
		}
		else
		{
			if (client_rd_blk(&cl, &blk, 0) != 0)
			{
				return -EIO;
			}
		}

		memcpy(&blk.data[offset], buf, size);

		if (client_wr_blk(&cl, &blk, 0) != 0)
		{
			return -EIO;
		}
	}
	else
	{
		size = 0;
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

	ret = client_start(&cl, "password123");
	if (ret != 0)
	{
		ret = EXIT_FAILURE;
		goto exit;
	}

	ret = fuse_main(argc, argv, &fs_ops, NULL);

exit:
	client_stop(&cl);

	return ret;
}
