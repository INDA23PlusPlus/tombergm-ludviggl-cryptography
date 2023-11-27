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
#include <err.h>
#include <mtree.h>
#include "cache.h"
#include "client.h"
#include "fs.h"

static int update_top(client_t *cl, hash_t *hash)
{
	int ret = 0;

	try_fd(0, lseek, cl->hash_fd, 0, SEEK_SET);
	try_io(0, write, cl->hash_fd, hash, sizeof*(hash));

exit:
	return ret;
}

static int verify_top(client_t *cl, hash_t *hash)
{
	int	ret = 0;
	hash_t	buf;

	try_fd(0, lseek, cl->hash_fd, 0, SEEK_SET);
	try_io(0, read, cl->hash_fd, buf, sizeof(buf));
	try_fn(0, memcmp, buf, hash, sizeof*(hash));

exit:
	return ret;
}

static int compute_top(client_t *cl, blk_t *blk, blk_id_t blk_id, hash_t *hash)
{
	int		ret	= 0;
	node_id_t	node_id	= mtree_blk_from_depth(MTREE_DEPTH, blk_id);

	crypto_generichash(	(void *)       hash, sizeof*(hash),
				(const void *) blk , sizeof*(blk) ,
				NULL               , 0);

	while (node_id != 0)
	{
		hash_t	pair[2];
		int	node_par = (node_id ^ 1) & 1;

		try_io(0, recv, cl->sock_fd, pair[node_par ^ 1], sizeof*(pair),
			MSG_WAITALL);

		memcpy(pair[node_par], hash, sizeof*(pair));

		crypto_generichash(	(void *) hash, sizeof*(hash),
					(void *) pair, sizeof (pair),
					NULL         , 0);

		node_id = mtree_parent(node_id);
	}

exit:
	return ret;
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
		cache_del(cl->sb_cache);
	}

	if (cl->dir_cache != NULL)
	{
		cache_del(cl->dir_cache);
	}

	if (cl->reg_cache != NULL)
	{
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
	cmd_t	cmd	= CMD_SYNC;
	hash_t	hash;

	cl->hash_fd = try_fd(0, openat, cl->root_fd, "hash", flags, mode);
	try_fn(0, ftruncate, cl->hash_fd, sizeof(hash));
	try_io(0, send, cl->sock_fd, &cmd, sizeof(cmd), 0);
	try_io(0, recv, cl->sock_fd, &hash, sizeof(hash), MSG_WAITALL);
	try_fn(0, update_top, cl, &hash);
	try_fn(0, fs_init, cl, 4);

exit:
	return ret;
}

int client_start(client_t *cl, const char *host, const char *root_path,
		const char *pw)
{
	int			ret				= 0;
	struct protoent *	tcp				= NULL;
	struct addrinfo *	addrinfo			= NULL;
	char			salt[crypto_pwhash_SALTBYTES]	= { 0 };
	struct stat		statbuf;

	client_reset(cl);

	if (sodium_init() < 0)
	{
		fail_fn(0, sodium_init);
	}

	tcp = try_ptr(0, getprotobyname, "tcp");

	cl->sock_fd = try_fd(0, socket, AF_INET, SOCK_STREAM, tcp->p_proto);

	try_fn(0, getaddrinfo, host, "1311", NULL, &addrinfo);
	try_fn(0, connect, cl->sock_fd,
		addrinfo->ai_addr, addrinfo->ai_addrlen);

	try_fn(ENOMEM, crypto_pwhash,	(void *) cl->key, sizeof(cl->key),
					(void *) pw     , strlen(pw   ),
					(void *) salt   ,
					crypto_pwhash_OPSLIMIT_INTERACTIVE,
					crypto_pwhash_MEMLIMIT_INTERACTIVE,
					crypto_pwhash_ALG_DEFAULT);

	randombytes_buf(cl->salt, sizeof(cl->salt));

	cl->sb_cache	= try_ptr(ENOMEM, cache_new, cl, 4);
	cl->dir_cache	= try_ptr(ENOMEM, cache_new, cl, 4);
	cl->reg_cache	= try_ptr(ENOMEM, cache_new, cl, 4);

	if (stat(root_path, &statbuf) != 0)
	{
		try_fn(0, mkdir, root_path, 0700);
	}

	cl->root_fd = try_fd(0, open, root_path, O_RDONLY);

	if (fstatat(cl->root_fd, "hash", &statbuf, 0) != 0)
	{
		try_fn(0, client_new_sys, cl);
	}
	else
	{
		cl->hash_fd = try_fd(0, openat, cl->root_fd, "hash", O_RDWR);
	}

exit:
	if (ret != 0)
	{
		client_dstr(cl);
	}

	if (addrinfo != NULL)
	{
		freeaddrinfo(addrinfo);
	}

	return ret;
}

int client_stop(client_t *cl)
{
	int	ret	= 0;

	ret = client_flush_all(cl);

	client_dstr(cl);

	return ret;
}

int client_rd_blk(client_t *cl, blk_t *blk, blk_id_t id)
{
	int	ret	= 0;
	cmd_t	cmd	= CMD_RD_BLK;
	hash_t	hash;

	try_io(0, send, cl->sock_fd, &cmd, sizeof(cmd), MSG_MORE);
	try_io(0, send, cl->sock_fd, &id, sizeof(id), 0);
	try_io(0, recv, cl->sock_fd, &cmd, sizeof(cmd), MSG_WAITALL);

	if (cmd == CMD_NDAT)
	{
		memset(blk, 0, sizeof*(blk));
	}
	else if (cmd == CMD_RD_BLK)
	{
		try_io(0, recv, cl->sock_fd, blk, sizeof*(blk), MSG_WAITALL);
	}
	else
	{
		fail_fn(EINVAL, __func__);
	}

	try_fn(0, compute_top, cl, blk, id, &hash);
	try_fn(0, verify_top, cl, &hash);

	if (cmd == CMD_RD_BLK)
	{
		ret = blk_decrypt(
			(void *) blk->data, 	NULL,
			NULL,
			(void *) blk->data,	sizeof(blk->data) +
						sizeof(blk->auth),
			NULL, 0,
			(void *) blk->salt,
			(void *) cl->key);
	}

exit:
	return ret;
}

int client_wr_blk(client_t *cl, blk_t *blk, blk_id_t id)
{
	int	ret	= 0;
	cmd_t	cmd	= CMD_WR_BLK;
	hash_t	hash;

	try_io(0, send, cl->sock_fd, &cmd, sizeof(cmd), MSG_MORE);
	try_io(0, send, cl->sock_fd, &id, sizeof(id), MSG_MORE);

	memcpy(blk->salt, cl->salt, sizeof(cl->salt));

	blk_encrypt(	(void *) blk->data, NULL,
			(void *) blk->data, sizeof(blk->data),
			NULL, 0,
			NULL,
			(void *) blk->salt,
			(void *) cl->key);

	try_io(0, send, cl->sock_fd, blk, sizeof*(blk), 0);

	try_fn(0, compute_top, cl, blk, id, &hash);
	try_fn(0, update_top, cl, &hash);

exit:
	return ret;
}

int client_flush_all(client_t *cl)
{
	int ret = 0;

	try_fn(0, cache_flush, cl->sb_cache);
	try_fn(0, cache_flush, cl->dir_cache);
	try_fn(0, cache_flush, cl->reg_cache);

exit:
	return ret;
}
