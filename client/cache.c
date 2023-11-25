#include <stddef.h>
#include <string.h>
#include <blk.h>
#include "cache.h"
#include "client.h"

static inline int cblk_valid(const cblk_t *cblk)
{
	return cblk->flags & CACHE_VALID;
}

static inline int cblk_dirty(const cblk_t *cblk)
{
	return cblk->flags & CACHE_DIRTY;
}

static inline void cblk_set_valid(cblk_t *cblk, int valid)
{
	if (valid)
	{
		cblk->flags |= CACHE_VALID;
	}
	else
	{
		cblk->flags &= ~CACHE_VALID;
	}
}

static inline void cblk_set_dirty(cblk_t *cblk, int dirty)
{
	if (dirty)
	{
		cblk->flags |= CACHE_DIRTY;
	}
	else
	{
		cblk->flags &= ~CACHE_DIRTY;
	}
}

static void cblk_init(cblk_t *cblk)
{
	cblk->flags = 0;
}

static int cblk_flush(cblk_t *cblk, client_t *cl)
{
	int	ret	= 0;
	blk_t	blk;

	if (cblk_valid(cblk) && cblk_dirty(cblk))
	{
		memcpy(blk.data, cblk->data, sizeof(cblk->data));

		ret = client_wr_blk(cl, &blk, cblk->id);

		if (ret == 0)
		{
			cblk_set_dirty(cblk, 0);
		}
	}

	return ret;
}

static int cblk_fetch(cblk_t *cblk, blk_id_t id, client_t *cl)
{
	int	ret	= cblk_flush(cblk, cl);
	blk_t	blk;

	if (ret != 0)
	{
		return ret;
	}

	ret = client_rd_blk(cl, &blk, id);

	if (ret != 0)
	{
		return ret;
	}

	memcpy(cblk->data, blk.data, sizeof(cblk->data));

	cblk->id = id;
	cblk->flags = CACHE_VALID;

	return ret;
}

static cblk_t *cache_find_blk(cache_t *cache, blk_id_t id)
{
	for (int i = 0; i < cache->n_blk; i++)
	{
		cblk_t *cblk = &cache->blk[i];

		if (cblk_valid(cblk) && cblk->id == id)
		{
			return cblk;
		}
	}

	return NULL;
}

static cblk_t *cache_find_ptr(cache_t *cache, void *ptr)
{
	for (int i = 0; i < cache->n_blk; i++)
	{
		cblk_t *cblk = &cache->blk[i];

		if (	cblk_valid(cblk)				&&
			(char *) ptr >=	&cblk->data[0]			&&
			(char *) ptr <	&cblk->data[BLK_DATA_LEN]	)
		{
			return cblk;
		}
	}

	return NULL;
}

cache_t *cache_new(client_t *cl, int n_blk)
{
	cache_t *cache = malloc(sizeof(cache_t) + sizeof(cblk_t) * n_blk);

	if (cache != NULL)
	{
		cache->cl = cl;
		cache->n_blk = n_blk;

		for (int i = 0; i < n_blk; i++)
		{
			cblk_init(&cache->blk[i]);
		}
	}

	return cache;
}

void cache_del(cache_t *cache)
{
	free(cache);
}

void *cache_get_blk(cache_t *cache, blk_id_t id)
{
	cblk_t *cblk = cache_find_blk(cache, id);

	if (cblk != NULL)
	{
		return cblk->data;
	}

	cblk = &cache->blk[id % cache->n_blk];

	if (cblk_fetch(cblk, id, cache->cl) == 0)
	{
		return cblk->data;
	}

	return NULL;
}

void *cache_claim_blk(cache_t *cache, blk_id_t id)
{
	cblk_t *cblk = cache_find_blk(cache, id);

	if (cblk != NULL)
	{
		return cblk->data;
	}

	cblk = &cache->blk[id % cache->n_blk];

	cblk_flush(cblk, cache->cl);

	cblk->id = id;
	cblk->flags = CACHE_VALID | CACHE_DIRTY;

	return cblk->data;
}

void cache_dirty_blk(cache_t *cache, blk_id_t id)
{
	cblk_t *cblk = cache_find_blk(cache, id);

	if (cblk != NULL)
	{
		cblk_set_dirty(cblk, 1);
	}
}

void cache_dirty_ptr(cache_t *cache, void *ptr)
{
	cblk_t *cblk = cache_find_ptr(cache, ptr);

	if (cblk != NULL)
	{
		cblk_set_dirty(cblk, 1);
	}
}

int cache_flush_blk(cache_t *cache, blk_id_t id)
{
	int ret = 0;

	cblk_t *cblk = cache_find_blk(cache, id);

	if (cblk != NULL)
	{
		ret = cblk_flush(cblk, cache->cl);
	}

	return ret;
}

int cache_flush_ptr(cache_t *cache, void *ptr)
{
	int ret = 0;

	cblk_t *cblk = cache_find_ptr(cache, ptr);

	if (cblk != NULL)
	{
		ret = cblk_flush(cblk, cache->cl);
	}

	return ret;
}

int cache_flush(cache_t *cache)
{
	int ret = 0;

	for (int i = 0; i < cache->n_blk; i++)
	{
		ret = cblk_flush(&cache->blk[i], cache->cl);

		if (ret != 0)
		{
			return ret;
		}
	}

	return ret;
}
