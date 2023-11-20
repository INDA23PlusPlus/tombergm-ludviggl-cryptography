#ifndef MTREE_H
#define MTREE_H

#include <sodium.h>
#include <blk.h>

#define MTREE_HASH_LEN	crypto_generichash_BYTES

typedef struct
{
	char		hash[MTREE_HASH_LEN];
} mtree_node_t;

typedef struct
{
	unsigned	depth;
	mtree_node_t	nodes[];
} mtree_t;

mtree_t *	mtree_new		(unsigned depth);
void		mtree_update_node	(mtree_t *mtree, blk_id_t id);
void		mtree_set_blk		(mtree_t *mtree, blk_id_t blk_id,
					const blk_t *blk);
void        mtree_send_chain(mtree_t *mtree, blk_id_t blk_id);

static inline blk_id_t mtree_parent(blk_id_t node)
{
	return (node - 1) >> 1;
}

static inline blk_id_t mtree_blk(const mtree_t *mtree, blk_id_t blk_id)
{
	return ((blk_id_t) 1 << mtree->depth) - 1 + blk_id;
}

static inline blk_id_t mtree_sibling(mtree_t *mtree, blk_id_t node_id)
{
    return ((node_id - 1) ^ (blk_id_t) 1) + 1;
}

#endif
