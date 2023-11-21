#ifndef CACHE_H
#define CACHE_H

#include <blk.h>

#define CACHE_VALID	1u
#define CACHE_DIRTY	2u

typedef struct client client_t;

typedef struct
{
	blk_id_t	id;
	unsigned	flags;
	char		data[BLK_DATA_LEN];
} cblk_t;

typedef struct cache
{
	client_t *	cl;
	int		n_blk;
	cblk_t		blk[];
} cache_t;

cache_t *	cache_new(client_t *cl, int n_blk);
void *		cache_get_blk(cache_t *cache, blk_id_t id);
void		cache_dirty_blk(cache_t *cache, blk_id_t id);
int		cache_flush_blk(cache_t *cache, blk_id_t id);
int		cache_flush(cache_t *cache);

#endif
