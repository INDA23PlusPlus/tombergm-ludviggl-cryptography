#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
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
		ret = send(sv->sock, node, sizeof*(node), flags);

		if (ret != sizeof*(node))
		{
			perror("error: send");
			return -1;
		}
	}

	return 0;
}

static int server_rd_blk(server_t *sv)
{
	int		ret;
	blk_id_t	id;
	blk_t		blk;

	ret = recv(sv->sock, &id, sizeof(id), MSG_WAITALL);
	if (ret != sizeof(id))
	{
		perror("error: recv");
		return -1;
	}

	char fname[32];
	sprintf(fname, "%016" PRIx64, id);

	int fd = open(fname, O_RDONLY);
	if (fd == -1)
	{
		perror("error: open");
		return -1;
	}

	if (read(fd, &blk, sizeof(blk)) != sizeof(blk))
	{
		close(fd);
		perror("error: read");
		return -1;
	}

	close(fd);

	if (send(sv->sock, &blk, sizeof(blk), 0) != sizeof(blk))
	{
		perror("error: send");
		return -1;
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
	blk_id_t	id;
	blk_t		blk;

	ret = recv(sv->sock, &id, sizeof(id), MSG_WAITALL);
	if (ret != sizeof(id))
	{
		perror("error: recv");
		return -1;
	}

	ret = recv(sv->sock, &blk, sizeof(blk), MSG_WAITALL);
	if (ret != sizeof(blk))
	{
		perror("error: recv");
		return -1;
	}

	char fname[32];
	sprintf(fname, "%016" PRIx64, id);

	int fd = open(fname, O_WRONLY | O_CREAT, 0644);
	if (fd == -1)
	{
		perror("error: open");
		return -1;
	}

	if (write(fd, &blk, sizeof(blk)) != sizeof(blk))
	{
		perror("error: write");
		close(fd);
		return -1;
	}

	close(fd);

	mtree_set_blk(sv->mtree, id, &blk);

	ret = send_mtree(sv, id);
	if (ret != 0)
	{
		return ret;
	}

	return 0;
}

int server_start(server_t *sv, int sock)
{
	sv->sock	= sock;
	sv->mtree	= mtree_new(MTREE_DEPTH);

	if (sv->mtree == NULL)
	{
		errno = ENOMEM;
		perror("error: mtree_new");
		return -1;
	}

	return 0;
}

int server_stop(server_t *sv)
{
	if (sv->mtree != NULL)
	{
		mtree_del(sv->mtree);
	}
	if (sv->sock != -1)
	{
		close(sv->sock);
	}

	return 0;
}

int server_run(server_t *sv)
{
	int	ret;
	cmd_t	cmd;

	for (;;)
	{
		errno = 0;

		ret = recv(sv->sock, &cmd, sizeof(cmd), MSG_WAITALL);

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
			case CMD_RD_BLK	: ret = server_rd_blk(sv);	break;
			case CMD_WR_BLK	: ret = server_wr_blk(sv);	break;
		}

		if (ret != 0)
		{
			return ret;
		}
	}
}
