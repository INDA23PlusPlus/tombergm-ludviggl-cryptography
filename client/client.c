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
#include "mtree.h"

#define CHAIN_LEN (MTREE_DEPTH - 1)

static int update_hash(char *hash);
static int verify_hash(char *hash);
static void compute_chain(char *out, char *in, char *chain, size_t len,
				blk_id_t blk_id);

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

    char blk_hash[MTREE_HASH_LEN];
    crypto_generichash((void *) blk_hash,
                       sizeof(blk_hash),
                       (const void *) blk,
                       sizeof(*blk),
                       NULL,
                       0);

    char chain[MTREE_HASH_LEN * CHAIN_LEN];
    int chain_len = CHAIN_LEN;

    ret = recv(cl->sock, chain, chain_len, 0);
    if (ret != chain_len)
    {
        perror("error: recv");
        return -1;
    }

    char hash[MTREE_HASH_LEN];
    compute_chain(hash,
                  blk_hash,
                  chain,
                  chain_len,
                  id);

    ret = verify_hash(hash);
    if (ret != 0)
    {
        perror("error: verify_hash");
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

    char blk_hash[MTREE_HASH_LEN];
    crypto_generichash((void *) blk_hash,
                       sizeof(blk_hash),
                       (const void *) blk,
                       sizeof(*blk),
                       NULL,
                       0);

	ret = send(cl->sock, blk, sizeof*(blk), 0);
	if (ret != sizeof*(blk))
	{
		perror("error: send");
		return -1;
	}

    char chain[MTREE_HASH_LEN * CHAIN_LEN];
    int chain_len = CHAIN_LEN;

    ret = recv(cl->sock, chain, chain_len, 0);
    if (ret != chain_len)
    {
        perror("error: recv");
        return -1;
    }

    char hash[MTREE_HASH_LEN];
    compute_chain(hash,
                  blk_hash,
                  chain,
                  chain_len,
                  id);

    ret = update_hash(hash);
    if (ret != 0)
    {
        perror("error: update_hash");
        return -1;
    }

	return 0;
}

static int update_hash(char *hash)
{
    int ret;
    FILE *f;

    f = fopen("hash", "wb");

    if (f == NULL)
    {
        perror("error: fopen");
        return -1;
    }

    ret = fwrite(hash,
                MTREE_HASH_LEN,
                1,
                f);
    fclose(f);

    if (ret != 1)
    {
        perror("error: fwrite");
        return -1;
    }

    return 0;
}

static int verify_hash(char *hash)
{
    int ret;
    FILE *f;
    char buf[MTREE_HASH_LEN];

    f = fopen("hash", "rb");

    if (f == NULL)
    {
        perror("error: fopen");
        return -1;
    }

    ret = fread(buf,
                sizeof(buf),
                1,
                f);
    fclose(f);

    if (ret != 1)
    {
        perror("error: fread");
        return -1;
    }

    ret = memcmp(buf, hash, sizeof(buf));
    if (ret != 0)
    {
        // possibly corrupted
        perror("error: memcmp");
        return -1;
    }

    return 0;
}

static void compute_chain(char *out, char *in, char *chain, size_t len, blk_id_t blk_id)
{
    char buf[MTREE_HASH_LEN * 2];
    blk_id_t node_id;
    size_t prev_offset, chain_offset;

    node_id = mtree_blk_from_depth(MTREE_DEPTH, blk_id);

    memcpy(out, in, MTREE_HASH_LEN);

    for (int i = 0; i < len; i++)
    {
        // VERIFIERA DETTA !
        if (node_id % 2 == 0)
        {
            prev_offset = MTREE_HASH_LEN;
            chain_offset = 0;
        }
        else
        {
            prev_offset = 0;
            chain_offset = MTREE_HASH_LEN;
        }

        memcpy(buf + prev_offset,
               out,
               MTREE_HASH_LEN);

        memcpy(buf + chain_offset,
               chain + i * MTREE_HASH_LEN,
               MTREE_HASH_LEN);

        crypto_generichash(out,
                           MTREE_HASH_LEN,
                           buf,
                           MTREE_HASH_LEN * 2,
                           NULL,
                           0);

        node_id = mtree_parent(node_id);
    }
}
