#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sodium.h>
#include <blk.h>
#include <cmd.h>
#include "cache.h"
#include "client.h"
#include "mtree.h"
#include "err.h"
#include "fs.h"

static int update_top(client_t *cl, char (*hash)[MTREE_HASH_LEN])
{
	int	ret;

	ret = lseek(cl->hash_fd, 0, SEEK_SET);
	if (ret == -1)
	{
		perror("error: lseek");
		return -1;
	}

	ret = write(cl->hash_fd, hash, sizeof*(hash));
	if (ret != sizeof*(hash))
	{
		perror("error: write");
		return -1;
	}

	return 0;
}

static int verify_top(client_t *cl, char (*hash)[MTREE_HASH_LEN])
{
	int	ret;
	char	buf[MTREE_HASH_LEN];

	ret = lseek(cl->hash_fd, 0, SEEK_SET);
	if (ret == -1)
	{
		perror("error: lseek");
		return -1;
	}

	ret = read(cl->hash_fd, buf, sizeof(buf));
	if (ret != sizeof(buf))
	{
		perror("error: read");
		return -1;
	}

	ret = memcmp(buf, hash, sizeof*(hash));
	if (ret != 0)
	{
		errno = EBADMSG;
		perror("error: memcmp");
		return -1;
	}

	return 0;
}

static int compute_top(client_t *cl, blk_t *blk, blk_id_t blk_id,
			char (*hash)[MTREE_HASH_LEN])
{
	int		ret	= 0;
	node_id_t	node_id	= mtree_blk_from_depth(MTREE_DEPTH, blk_id);

	crypto_generichash(	(void *)       hash, sizeof*(hash),
				(const void *) blk , sizeof*(blk) ,
				NULL               , 0);

	while (node_id != 0)
	{
		char	pair[2][MTREE_HASH_LEN];
		int	node_par = (node_id ^ 1) & 1;

		ret = recv(cl->sock_fd, pair[node_par ^ 1], sizeof*(pair), 0);

		if (ret != sizeof*(pair))
		{
			perror("error: recv");
			return -1;
		}

		memcpy(pair[node_par], hash, sizeof*(pair));

		crypto_generichash(	(void *) hash, sizeof*(hash),
					(void *) pair, sizeof (pair),
					NULL         , 0);

		node_id = mtree_parent(node_id);
	}

	return 0;
}

static int client_reset(client_t *cl)
{
	cl->sock_fd	= -1;
	cl->root_fd	= -1;
	cl->hash_fd	= -1;
	cl->sb_cache	= NULL;
	cl->dir_cache	= NULL;
	cl->reg_cache	= NULL;

	return 0;
}

static int client_dstr(client_t *cl)
{
	if (cl->sb_cache != NULL)
	{
		cache_flush(cl->sb_cache);
		cache_del(cl->sb_cache);
	}
	if (cl->dir_cache != NULL)
	{
		cache_flush(cl->dir_cache);
		cache_del(cl->dir_cache);
	}
	if (cl->reg_cache != NULL)
	{
		cache_flush(cl->reg_cache);
		cache_del(cl->reg_cache);
	}
	if (cl->sock_fd != -1)
	{
		close(cl->sock_fd);
	}
	if (cl->root_fd != -1)
	{
		close(cl->root_fd);
	}
	if (cl->hash_fd != -1)
	{
		close(cl->hash_fd);
	}

	return 0;
}

static int client_new_sys(client_t *cl)
{
	int	ret	= 0;
	int	flags	= O_RDWR | O_CREAT | O_EXCL;
	mode_t	mode	= 0600;
	cmd_t	cmd;
	char	hash[MTREE_HASH_LEN];

	cl->hash_fd = openat(cl->root_fd, "hash", flags, mode);
	if (cl->hash_fd == -1)
	{
		perror("error: openat");
		ret = -1;
		goto exit;
	}
	ret = ftruncate(cl->hash_fd, sizeof(hash));
	if (ret != 0)
	{
		perror("error: ftruncate");
		goto exit;
	}

	cmd = CMD_SYNC;
	ret = send(cl->sock_fd, &cmd, sizeof(cmd), 0);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		ret = -1;
		goto exit;
	}

	ret = recv(cl->sock_fd, &hash, sizeof(hash), 0);
	if (ret != sizeof(hash))
	{
		perror("error: recv");
		ret = -1;
		goto exit;
	}

	ret = update_top(cl, &hash);
	if (ret != 0)
	{
		goto exit;
	}

    ret = fs_init(cl, 4);
    if (ret != 0)
    {
        ret = -1;
        goto exit;
    }

	ret = 0;

exit:
	if (ret != 0)
	{
		client_dstr(cl);
	}

	return ret;
}

int client_start(client_t *cl, const char *host, const char *root_path,
		const char *pw)
{
	int			ret		= 0;
	struct protoent *	tcp		= NULL;
	struct addrinfo *	addrinfo	= NULL;

	client_reset(cl);

	if (sodium_init() < 0)
	{
		fprintf(stderr, "error: sodium_init failed");
		ret = -1;
		goto exit;
	}

	tcp = getprotobyname("tcp");
	if (tcp == NULL)
	{
		perror("error: getprotobyname");
		ret =  -1;
		goto exit;
	}

	cl->sock_fd = socket(AF_INET, SOCK_STREAM, tcp->p_proto);
	if (cl->sock_fd == -1)
	{
		perror("error: socket");
		ret =  -1;
		goto exit;
	}

	ret = getaddrinfo(host, "1311", NULL, &addrinfo);
	if (ret != 0)
	{
		fprintf(stderr, "error: hostname lookup failed\n");
		goto exit;
	}

	ret = connect(cl->sock_fd, addrinfo->ai_addr, addrinfo->ai_addrlen);
	freeaddrinfo(addrinfo);

	if (ret != 0)
	{
		perror("error: connect");
		ret =  -1;
		goto exit;
	}

	char salt[crypto_pwhash_SALTBYTES] = { 0 };

	ret = crypto_pwhash(	(void *) cl->key, sizeof(cl->key),
				(void *) pw     , strlen(pw   ),
				(void *) salt   ,
				crypto_pwhash_OPSLIMIT_INTERACTIVE,
				crypto_pwhash_MEMLIMIT_INTERACTIVE,
				crypto_pwhash_ALG_DEFAULT);
	if (ret != 0)
	{
		errno = ENOMEM;
		perror("error: crypto_pwhash");
		ret =  -1;
		goto exit;
	}

	randombytes_buf(cl->salt, sizeof(cl->salt));

	cl->sb_cache	= cache_new(cl, 4);
	cl->dir_cache	= cache_new(cl, 4);
	cl->reg_cache	= cache_new(cl, 4);

	if (	cl->sb_cache == NULL	||
		cl->dir_cache == NULL	||
		cl->reg_cache == NULL	)
	{
		errno = ENOMEM;
		perror("error: cache_new");
		ret = -1;
		goto exit;
	}

	cl->root_fd = open(root_path, O_RDONLY);
	if (cl->root_fd == -1 && errno == ENOENT)
	{
		ret = mkdir(root_path, 0700);
		if (ret != 0)
		{
			perror("error: mkdir");
			goto exit;
		}

		cl->root_fd = open(root_path, O_RDONLY);
	}
	if (cl->root_fd == -1)
	{
		perror("error: open");
		ret = -1;
		goto exit;
	}

	cl->hash_fd = openat(cl->root_fd, "hash", O_RDWR);
	if (cl->hash_fd == -1 && errno == ENOENT)
	{
		return client_new_sys(cl);
	}
	if (cl->hash_fd == -1)
	{
		perror("error: open");
		ret = -1;
		goto exit;
	}

	ret = 0;

exit:
	if (ret != 0)
	{
		client_dstr(cl);
	}

	return ret;
}

int client_stop(client_t *cl)
{
	return client_dstr(cl);
}

int client_rd_blk(client_t *cl, blk_t *blk, blk_id_t id)
{
	int	ret;
	cmd_t	cmd = CMD_RD_BLK;

	ret = send(cl->sock_fd, &cmd, sizeof(cmd), MSG_MORE);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		return -1;
	}

	ret = send(cl->sock_fd, &id, sizeof(id), 0);
	if (ret != sizeof(id))
	{
		perror("error: send");
		return -1;
	}

	ret = recv(cl->sock_fd, &cmd, sizeof(cmd), MSG_WAITALL);
	if (ret != sizeof(cmd))
	{
		perror("error: recv");
		return -1;
	}

	if (cmd == CMD_NDAT)
	{
		memset(blk, 0, sizeof*(blk));
	}
	else if (cmd == CMD_RD_BLK)
	{
		ret = recv(cl->sock_fd, blk, sizeof*(blk), MSG_WAITALL);
		if (ret != sizeof*(blk))
		{
			perror("error: recv");
			return -1;
		}
	}
	else
	{
		errno = EINVAL;
		perror("error: client_rd_blk");
		return -1;
	}

	char hash[MTREE_HASH_LEN];
	ret = compute_top(cl, blk, id, &hash);
	if (ret != 0)
	{
		return ret;
	}

	ret = verify_top(cl, &hash);
	if (ret != 0)
	{
		return ret;
	}

	if (cmd == CMD_RD_BLK)
	{
		ret = blk_decrypt(
			(void *) blk->data, NULL,
			NULL,
			(void *) blk->data,	sizeof(blk->data) +
						sizeof(blk->auth),
			NULL, 0,
			(void *) blk->salt,
			(void *) cl->key);
	}

	return 0;
}

int client_wr_blk(client_t *cl, blk_t *blk, blk_id_t id)
{
	int ret;

	cmd_t cmd = CMD_WR_BLK;

	ret = send(cl->sock_fd, &cmd, sizeof(cmd), MSG_MORE);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		return -1;
	}

	ret = send(cl->sock_fd, &id, sizeof(id), MSG_MORE);
	if (ret != sizeof(id))
	{
		perror("error: send");
		return -1;
	}

	memcpy(blk->salt, cl->salt, sizeof(cl->salt));

	blk_encrypt(
		(void *) blk->data, NULL,
		(void *) blk->data, sizeof(blk->data),
		NULL, 0,
		NULL,
		(void *) blk->salt,
		(void *) cl->key);

	ret = send(cl->sock_fd, blk, sizeof*(blk), 0);
	if (ret != sizeof*(blk))
	{
		perror("error: send");
		return -1;
	}

	char hash[MTREE_HASH_LEN];
	ret = compute_top(cl, blk, id, &hash);
	if (ret != 0)
	{
		return ret;
	}

	ret = update_top(cl, &hash);
	if (ret != 0)
	{
		return ret;
	}

	return 0;
}

int client_flush_all(client_t *cl)
{
    int ret;

    if ((ret = cache_flush(cl->sb_cache)))  return ret;
    if ((ret = cache_flush(cl->dir_cache))) return ret;
    if ((ret = cache_flush(cl->reg_cache))) return ret;

    return 0;
}
