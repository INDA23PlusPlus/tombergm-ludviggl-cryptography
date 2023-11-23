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
		ret = send(sv->sock_fd, node, sizeof*(node), flags);

		if (ret != sizeof*(node))
		{
			perror("error: send");
			return -1;
		}
	}

	return 0;
}

static int server_synccl(server_t *sv)
{
	int		ret	= 0;
	mtree_node_t *	node	= &sv->mtree->nodes[0];

	ret = send(sv->sock_fd, node, sizeof*(node), 0);

	if (ret != sizeof*(node))
	{
		perror("error: send");
		return -1;
	}

	return 0;
}

static int server_rd_blk(server_t *sv)
{
	static blk_t	null_blk;
	int		ret;
	int		len;
	blk_id_t	id;
	blk_t		blk;
	cmd_t		cmd;

	len = sizeof(id);
	if (recv(sv->sock_fd, &id, len, MSG_WAITALL) != len)
	{
		perror("error: recv");
		return -1;
	}

	len = sizeof(blk.data);
	ret = lseek(sv->data_fd, (off_t) len * id, SEEK_SET);
	if (ret == -1)
	{
		perror("error: lseek");
		return -1;
	}

	if (read(sv->data_fd, &blk.data, len) != len)
	{
		perror("error: read");
		return -1;
	}

	len = sizeof(blk_t) - sizeof(blk.data);
	ret = lseek(sv->aead_fd, (off_t) len * id, SEEK_SET);
	if (ret == -1)
	{
		perror("error: lseek");
		return -1;
	}

	if (read(sv->aead_fd, &blk.auth, len) != len)
	{
		perror("error: read");
		return -1;
	}

	if (memcmp(&blk, &null_blk, sizeof(null_blk)) == 0)
	{
		cmd = CMD_NDAT;

		len = sizeof(cmd);
		if (send(sv->sock_fd, &cmd, len, MSG_MORE) != len)
		{
			perror("error: read");
			return -1;
		}
	}
	else
	{
		cmd = CMD_RD_BLK;

		len = sizeof(cmd);
		if (send(sv->sock_fd, &cmd, len, MSG_MORE) != len)
		{
			perror("error: read");
			return -1;
		}

		len = sizeof(blk);
		if (send(sv->sock_fd, &blk, len, MSG_MORE) != len)
		{
			perror("error: send");
			return -1;
		}
	}

	ret = send_mtree(sv, id);
	if (ret != 0)
	{
		return ret;
	}

	return 0;
}

static int server_wr_blk(server_t *sv)
{
	int		ret;
	int		len;
	blk_id_t	id;
	blk_t		blk;

	ret = recv(sv->sock_fd, &id, sizeof(id), MSG_WAITALL);
	if (ret != sizeof(id))
	{
		perror("error: recv");
		return -1;
	}

	ret = recv(sv->sock_fd, &blk, sizeof(blk), MSG_WAITALL);
	if (ret != sizeof(blk))
	{
		perror("error: recv");
		return -1;
	}

	len = sizeof(blk.data);
	ret = lseek(sv->data_fd, (off_t) len * id, SEEK_SET);
	if (ret == -1)
	{
		perror("error: lseek");
		return -1;
	}

	if (write(sv->data_fd, &blk.data, len) != len)
	{
		perror("error: write");
		return -1;
	}

	len = sizeof(blk_t) - sizeof(blk.data);
	ret = lseek(sv->aead_fd, (off_t) len * id, SEEK_SET);
	if (ret == -1)
	{
		perror("error: lseek");
		return -1;
	}

	if (write(sv->aead_fd, &blk.auth, len) != len)
	{
		perror("error: write");
		return -1;
	}

	mtree_set_blk(sv->mtree, id, &blk);

	ret = send_mtree(sv, id);
	if (ret != 0)
	{
		return ret;
	}

	return 0;
}

static void server_reset(server_t *sv)
{
	sv->sock_fd = -1;
	sv->root_fd = -1;
	sv->data_fd = -1;
	sv->aead_fd = -1;
	sv->tree_fd = -1;
	sv->mtree = NULL;
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
	char		hash[MTREE_HASH_LEN];

	sv->data_fd = openat(sv->root_fd, "data", flags, mode);
	if (sv->data_fd == -1)
	{
		perror("error: openat");
		ret = -1;
		goto exit;
	}
	ret = ftruncate(sv->data_fd, n_blk * BLK_DATA_LEN);
	if (ret != 0)
	{
		perror("error: ftruncate");
		goto exit;
	}

	sv->aead_fd = openat(sv->root_fd, "aead", flags, mode);
	if (sv->aead_fd == -1)
	{
		perror("error: openat");
		ret = -1;
		goto exit;
	}
	ret = ftruncate(sv->aead_fd, n_blk * (sizeof(blk_t) - BLK_DATA_LEN));
	if (ret != 0)
	{
		perror("error: ftruncate");
		goto exit;
	}

	sv->tree_fd = openat(sv->root_fd, "tree", flags, mode);
	if (sv->tree_fd == -1)
	{
		perror("error: openat");
		ret = -1;
		goto exit;
	}
	ret = ftruncate(sv->tree_fd, nodes * sizeof(mtree_node_t));
	if (ret != 0)
	{
		perror("error: ftruncate");
		goto exit;
	}

	sv->mtree = mtree_new(MTREE_DEPTH);
	if (sv->mtree == NULL)
	{
		errno = ENOMEM;
		perror("error: mtree_new");
		ret = -1;
		goto exit;
	}

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
	if (ret != 0)
	{
		server_dstr(sv);
	}

	return ret;
}

int server_start(server_t *sv, int sock_fd, const char *root_path)
{
	int		ret	= 0;
	node_id_t	nodes	= mtree_size_from_depth(MTREE_DEPTH);

	server_reset(sv);

	sv->sock_fd = sock_fd;

	sv->root_fd = open(root_path, O_RDONLY);
	if (sv->root_fd == -1 && errno == ENOENT)
	{
		ret = mkdir(root_path, 0700);
		if (ret != 0)
		{
			perror("error: mkdir");
			goto exit;
		}

		sv->root_fd = open(root_path, O_RDONLY);
	}
	if (sv->root_fd == -1)
	{
		perror("error: open");
		ret = -1;
		goto exit;
	}

	sv->data_fd = openat(sv->root_fd, "data", O_RDWR);
	if (sv->data_fd == -1 && errno == ENOENT)
	{
		return server_new_sys(sv);
	}
	if (sv->data_fd == -1)
	{
		perror("error: open");
		ret = -1;
		goto exit;
	}

	sv->aead_fd = openat(sv->root_fd, "aead", O_RDWR);
	if (sv->aead_fd == -1)
	{
		perror("error: open");
		ret = -1;
		goto exit;
	}

	sv->tree_fd = openat(sv->root_fd, "tree", O_RDWR);
	if (sv->tree_fd == -1)
	{
		perror("error: open");
		ret = -1;
		goto exit;
	}

	sv->mtree = mtree_new(MTREE_DEPTH);
	if (sv->mtree == NULL)
	{
		errno = ENOMEM;
		perror("error: mtree_new");
		ret = -1;
		goto exit;
	}

	ret = read(sv->tree_fd, sv->mtree->nodes,
			nodes * sizeof(mtree_node_t));
	if (ret != nodes * sizeof(mtree_node_t))
	{
		perror("error: read");
		ret = -1;
		goto exit;
	}

	ret = 0;

exit:
	if (ret != 0)
	{
		server_dstr(sv);
	}

	return ret;
}

int server_stop(server_t *sv)
{
	int	ret	= 0;
	size_t	len;

	if (lseek(sv->tree_fd, 0, SEEK_SET) == -1)
	{
		ret = -1;
		perror("error: lseek");
	}

	len = mtree_size(sv->mtree) * sizeof*(sv->mtree->nodes);
	if (write(sv->tree_fd, &sv->mtree->nodes, len) != len)
	{
		ret = -1;
		perror("error: write");
	}

	server_dstr(sv);

	return ret;
}

int server_run(server_t *sv)
{
	int	ret;
	cmd_t	cmd;

	for (;;)
	{
		errno = 0;

		ret = recv(sv->sock_fd, &cmd, sizeof(cmd), MSG_WAITALL);

		if (ret == 0 && errno == 0)
		{
			return 0;
		}

		if (ret != sizeof(cmd))
		{
			perror("error: recv");
			return -1;
		}

		switch (cmd)
		{
			case CMD_SYNC	: ret = server_synccl(sv);	break;
			case CMD_RD_BLK	: ret = server_rd_blk(sv);	break;
			case CMD_WR_BLK	: ret = server_wr_blk(sv);	break;
		}

		if (ret != 0)
		{
			return ret;
		}
	}
}
