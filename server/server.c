#include <stdio.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <blk.h>
#include <cmd.h>
#include <mtree.h>

static mtree_t *	mtree;

static int server_rd_blk(int sock)
{
	int		ret;
	blk_id_t	id;
	blk_t		blk;

	ret = recv(sock, &id, sizeof(id), MSG_WAITALL);
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

	if (send(sock, &blk, sizeof(blk), 0) != sizeof(blk))
	{
		perror("error: send");
		return -1;
	}

	return 0;
}

static int server_wr_blk(int sock)
{
	int		ret;
	blk_id_t	id;
	blk_t		blk;

	ret = recv(sock, &id, sizeof(id), MSG_WAITALL);
	if (ret != sizeof(id))
	{
		perror("error: recv");
		return -1;
	}

	ret = recv(sock, &blk, sizeof(blk), MSG_WAITALL);
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

	//mtree_set_blk(mtree, id, &blk);

	return 0;
}

int server_run(int sock)
{
	int	ret;
	cmd_t	cmd;

	for (;;)
	{
		ret = recv(sock, &cmd, sizeof(cmd), MSG_WAITALL);
		if (ret != sizeof(cmd))
		{
			perror("error: recv");
			return -1;
		}

		switch (cmd)
		{
			case CMD_RD_BLK	: ret = server_rd_blk(sock);	break;
			case CMD_WR_BLK	: ret = server_wr_blk(sock);	break;
		}

		if (ret != 0)
		{
			return ret;
		}
	}
}
