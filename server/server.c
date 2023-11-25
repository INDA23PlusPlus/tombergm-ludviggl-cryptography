#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <blk.h>
#include <cmd.h>
#include <err.h>
#include <mtree.h>
#include "server.h"

static int send_mtree(server_t *sv, blk_id_t blk_id)
{
	int		ret	= 0;
	node_id_t	node_id	= mtree_blk(sv->mtree, blk_id);

	while (node_id != 0)
	{
		mtree_node_t *	node;
		int		flags;

		node_id = mtree_sibling(sv->mtree, node_id);
		node = &sv->mtree->nodes[node_id];
		node_id = mtree_parent(node_id);

		flags = (node_id == 0 ? 0 : MSG_MORE);
		try_io(0, send, sv->sock_fd, node, sizeof*(node), flags);
	}

exit:
	return ret;
}

static int server_synccl(server_t *sv)
{
	int		ret	= 0;
	mtree_node_t *	node	= &sv->mtree->nodes[0];

	try_io(0, send, sv->sock_fd, node, sizeof*(node), 0);

exit:
	return ret;
}

static int server_rd_blk(server_t *sv)
{
	static blk_t	null_blk;
	int		ret	= 0;
	blk_id_t	id;
	blk_t		blk;
	cmd_t		cmd;

	try_io(0, recv, sv->sock_fd, &id, sizeof(id), MSG_WAITALL);

	log("read block %" PRIu64 "\n", id);

	try_fd(0, lseek, sv->data_fd, id * sizeof(blk.data), SEEK_SET);
	try_io(0, read, sv->data_fd, &blk.data, sizeof(blk.data));

	try_fd(0, lseek, sv->aead_fd, id * sizeof(blk.extr), SEEK_SET);
	try_io(0, read, sv->aead_fd, &blk.extr, sizeof(blk.extr));

	if (memcmp(&blk, &null_blk, sizeof(null_blk)) == 0)
	{
		cmd = CMD_NDAT;

		try_io(0, send, sv->sock_fd, &cmd, sizeof(cmd), MSG_MORE);
	}
	else
	{
		cmd = CMD_RD_BLK;

		try_io(0, send, sv->sock_fd, &cmd, sizeof(cmd), MSG_MORE);
		try_io(0, send, sv->sock_fd, &blk, sizeof(blk), MSG_MORE);
	}

	try_fn(0, send_mtree, sv, id);

exit:
	return ret;
}

static int server_wr_blk(server_t *sv)
{
	int		ret	= 0;
	blk_id_t	id;
	blk_t		blk;

	try_io(0, recv, sv->sock_fd, &id, sizeof(id), MSG_WAITALL);

	log("write block %" PRIu64 "\n", id);

	try_io(0, recv, sv->sock_fd, &blk, sizeof(blk), MSG_WAITALL);

	try_fd(0, lseek, sv->data_fd, id * sizeof(blk.data), SEEK_SET);
	try_io(0, write, sv->data_fd, &blk.data,
		sizeof(blk.data));

	try_fd(0, lseek, sv->aead_fd, id * sizeof(blk.extr), SEEK_SET);
	try_io(0, write, sv->aead_fd, &blk.extr, sizeof(blk.extr));

	mtree_set_blk(sv->mtree, id, &blk);
	try_fn(0, send_mtree, sv, id);

exit:
	return ret;
}

static void server_reset(server_t *sv)
{
	sv->sock_fd	= -1;
	sv->root_fd	= -1;
	sv->data_fd	= -1;
	sv->aead_fd	= -1;
	sv->tree_fd	= -1;
	sv->mtree	= NULL;
}

static int server_dstr(server_t *sv)
{
	if (sv->sock_fd != -1)
	{
		close(sv->sock_fd);
	}

	if (sv->root_fd != -1)
	{
		close(sv->root_fd);
	}

	if (sv->data_fd != -1)
	{
		close(sv->data_fd);
	}

	if (sv->aead_fd != -1)
	{
		close(sv->aead_fd);
	}

	if (sv->tree_fd != -1)
	{
		close(sv->tree_fd);
	}

	if (sv->mtree != NULL)
	{
		mtree_del(sv->mtree);
	}

	return 0;
}

static int server_new_sys(server_t *sv)
{
	int		ret	= 0;
	int		flags	= O_RDWR | O_CREAT | O_EXCL;
	mode_t		mode	= 0600;
	node_id_t	nodes	= mtree_size_from_depth(MTREE_DEPTH);
	blk_id_t	n_blk	= mtree_nblk_from_depth(MTREE_DEPTH);
	blk_t		blk;
	hash_t		hash;

	sv->data_fd = try_fd(0, openat, sv->root_fd, "data", flags, mode);
	try_fn(0, ftruncate, sv->data_fd, n_blk * BLK_DATA_LEN);

	sv->aead_fd = try_fd(0, openat, sv->root_fd, "aead", flags, mode);
	try_fn(0, ftruncate, sv->aead_fd, n_blk * BLK_EXTR_LEN);

	sv->tree_fd = try_fd(0, openat, sv->root_fd, "tree", flags, mode);
	try_fn(0, ftruncate, sv->tree_fd, nodes * sizeof(mtree_node_t));

	sv->mtree = try_ptr(0, mtree_new, MTREE_DEPTH);

	memset(&blk, 0, sizeof(blk));
	crypto_generichash(	(void *) &hash, sizeof(hash),
				(void *) &blk , sizeof(blk ),
				NULL, 0);

	for (blk_id_t blk_id = 0; blk_id != n_blk; blk_id++)
	{
		node_id_t	node_id	= mtree_blk(sv->mtree, blk_id);
		mtree_node_t *	node	= &sv->mtree->nodes[node_id];

		memcpy(&node->hash, &hash, sizeof(hash));
	}

	mtree_rebuild(sv->mtree);

exit:
	return ret;
}

int server_start(server_t *sv, int sock_fd, const char *root_path)
{
	int		ret	= 0;
	node_id_t	nodes	= mtree_size_from_depth(MTREE_DEPTH);
	struct stat	statbuf;

	server_reset(sv);

	sv->sock_fd = sock_fd;

	if (stat(root_path, &statbuf) != 0)
	{
		try_fn(0, mkdir, root_path, 0700);
	}

	sv->root_fd = try_fd(0, open, root_path, O_RDONLY);

	if (fstatat(sv->root_fd, "data", &statbuf, 0) != 0)
	{
		try_fn(0, server_new_sys, sv);
	}
	else
	{
		sv->data_fd = try_fd(0, openat, sv->root_fd, "data", O_RDWR);
		sv->aead_fd = try_fd(0, openat, sv->root_fd, "aead", O_RDWR);
		sv->tree_fd = try_fd(0, openat, sv->root_fd, "tree", O_RDWR);

		sv->mtree = try_ptr(ENOMEM, mtree_new, MTREE_DEPTH);
		try_io(0, read, sv->tree_fd, sv->mtree->nodes,
			nodes * sizeof(mtree_node_t));
	}

exit:
	if (ret != 0)
	{
		server_dstr(sv);
	}

	return ret;
}

int server_stop(server_t *sv)
{
	int		ret	= 0;

	try_fd(0, lseek, sv->tree_fd, 0, SEEK_SET);
	try_io(0, write, sv->tree_fd, &sv->mtree->nodes,
		mtree_size(sv->mtree) * sizeof*(sv->mtree->nodes));

exit:
	server_dstr(sv);

	return ret;
}

int server_run(server_t *sv)
{
	int	ret	= 0;
	cmd_t	cmd;

	for (;;)
	{
		errno = 0;
		ret = recv(sv->sock_fd, &cmd, sizeof(cmd), MSG_WAITALL);

		if (ret != sizeof(cmd))
		{
			if (errno == 0)
			{
				break;
			}
			else
			{
				fail_fn(0, recv);
			}
		}

		switch (cmd)
		{
			case CMD_SYNC	: try_fn(0, server_synccl, sv);	break;
			case CMD_RD_BLK	: try_fn(0, server_rd_blk, sv);	break;
			case CMD_WR_BLK	: try_fn(0, server_wr_blk, sv);	break;
		}
	}

exit:
	return ret;
}
