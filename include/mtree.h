#ifndef MTREE_H
#define MTREE_H

#include <stdint.h>
#include <sodium.h>
#include <blk.h>

#define MTREE_HASH_LEN	crypto_generichash_BYTES
#define MTREE_DEPTH     8

typedef uint64_t	node_id_t;
typedef char		hash_t[MTREE_HASH_LEN];

typedef struct
{
	hash_t		hash;
} mtree_node_t;

typedef struct
{
	unsigned	depth;
	mtree_node_t	nodes[];
} mtree_t;

mtree_t *	mtree_new		(unsigned depth);
void		mtree_del		(mtree_t *mtree);
void		mtree_rebuild		(mtree_t *mtree);
void		mtree_update_node	(mtree_t *mtree, node_id_t node_id);
void		mtree_set_blk		(mtree_t *mtree, node_id_t blk_id,
					const blk_t *blk);

static inline node_id_t mtree_parent(node_id_t node_id)
{
	return (node_id - 1) >> 1;
}

static inline node_id_t mtree_sibling(mtree_t *mtree, node_id_t node_id)
{
    return ((node_id - 1) ^ 1) + 1;
}

static inline node_id_t mtree_child(node_id_t node_id, int which)
{
	return (node_id << 1) + 1 + which;
}

static inline node_id_t mtree_blk_from_depth(unsigned depth, node_id_t blk_id)
{
	return ((node_id_t) 1 << depth) - 1 + blk_id;
}

static inline node_id_t mtree_blk(const mtree_t *mtree, blk_id_t blk_id)
{
	return mtree_blk_from_depth(mtree->depth, blk_id);
}

static inline node_id_t mtree_size_from_depth(unsigned depth)
{
	return ((node_id_t) 1 << (depth + 1)) - 1;
}

static inline node_id_t mtree_size(const mtree_t *mtree)
{
	return mtree_size_from_depth(mtree->depth);
}

static inline blk_id_t mtree_nblk_from_depth(unsigned depth)
{
	return (blk_id_t) 1 << depth;
}

static inline blk_id_t mtree_nblk(const mtree_t *mtree)
{
	return mtree_nblk_from_depth(mtree->depth);
}

#endif
