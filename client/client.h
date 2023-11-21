#ifndef CLIENT_H
#define CLIENT_H

#include <blk.h>

#define KEY_LEN	blk_crypto(_KEYBYTES)

typedef struct cache cache_t;

typedef struct client
{
	int			sock;
	char			key[KEY_LEN];
	char			salt[BLK_SALT_LEN];
	cache_t *		sb_cache;
	cache_t *		dir_cache;
	cache_t *		reg_cache;
} client_t;

int	client_start(client_t *cl, const char *pw);
int	client_stop(client_t *cl);
int	client_rd_blk(client_t *cl, blk_t *blk, blk_id_t id);
int	client_wr_blk(client_t *cl, blk_t *blk, blk_id_t id);

#endif
