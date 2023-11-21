#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sodium.h>
#include <blk.h>
#include <cmd.h>
#include "cache.h"
#include "client.h"

int client_start(client_t *cl, const char *pw)
{
	int			ret		= 0;
	struct protoent *	tcp		= NULL;
	struct sockaddr_in	addr;
	socklen_t		addrlen;

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

	cl->sock = socket(AF_INET, SOCK_STREAM, tcp->p_proto);
	if (cl->sock == -1)
	{
		perror("error: socket");
		ret =  -1;
		goto exit;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(1311);
	inet_aton("127.0.0.1", &addr.sin_addr);
	addrlen = sizeof(addr);

	ret = connect(cl->sock, (struct sockaddr *) &addr, addrlen);
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

	cl->sb_cache = cache_new(cl, 4);
	cl->dir_cache = cache_new(cl, 4);
	cl->reg_cache = cache_new(cl, 4);

	if (	cl->sb_cache == NULL	||
		cl->dir_cache == NULL	||
		cl->reg_cache == NULL	)
	{
		errno = ENOMEM;
		perror("error: cache_new");
		ret = -1;
		goto exit;
	}

	ret = 0;

exit:
	if (ret != 0)
	{
		if (cl->sock != -1)
		{
			close(cl->sock);
		}
		if (cl->sb_cache != NULL)
		{
			free(cl->sb_cache);
		}
		if (cl->dir_cache != NULL)
		{
			free(cl->dir_cache);
		}
		if (cl->reg_cache != NULL)
		{
			free(cl->reg_cache);
		}
	}

	return ret;
}

int client_stop(client_t *cl)
{
	if (cl->sock != -1)
	{
		close(cl->sock);
	}

	return 0;
}

int client_rd_blk(client_t *cl, blk_t *blk, blk_id_t id)
{
	int ret;

	cmd_t cmd = CMD_RD_BLK;

	ret = send(cl->sock, &cmd, sizeof(cmd), MSG_MORE);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		return -1;
	}

	ret = send(cl->sock, &id, sizeof(id), MSG_MORE);
	if (ret != sizeof(id))
	{
		perror("error: send");
		return -1;
	}

	ret = recv(cl->sock, blk, sizeof*(blk), MSG_WAITALL);
	if (ret != sizeof*(blk))
	{
		perror("error: recv");
		return -1;
	}

	ret = blk_decrypt(
		(void *) blk->data, NULL,
		NULL,
		(void *) blk->data, sizeof(blk->data) + sizeof(blk->auth),
		NULL, 0,
		(void *) blk->salt,
		(void *) cl->key);

	return 0;
}

int client_wr_blk(client_t *cl, blk_t *blk, blk_id_t id)
{
	int ret;

	cmd_t cmd = CMD_WR_BLK;

	ret = send(cl->sock, &cmd, sizeof(cmd), MSG_MORE);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		return -1;
	}

	ret = send(cl->sock, &id, sizeof(id), MSG_MORE);
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

	ret = send(cl->sock, blk, sizeof*(blk), 0);
	if (ret != sizeof*(blk))
	{
		perror("error: send");
		return -1;
	}

	return 0;
}
