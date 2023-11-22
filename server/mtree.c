#include <stddef.h>
#include <string.h>
#include <sodium.h>
#include <blk.h>
#include <mtree.h>

mtree_t *mtree_new(unsigned depth)
{
	node_id_t	nodes = mtree_size_from_depth(depth);
	mtree_t *	mtree;

	mtree = calloc(nodes, sizeof*(mtree->nodes));

	if (mtree != NULL)
	{
		mtree->depth = depth;
	}

	return mtree;
}

void mtree_del(mtree_t *mtree)
{
	free(mtree);
}

static void mtree_compute_node(mtree_t *mtree, node_id_t node_id)
{
	mtree_node_t (*node)[1]	= (void *) &mtree->nodes[node_id];
	mtree_node_t (*pair)[2]	= (void *) &mtree->nodes[(node_id << 1) + 1];

	crypto_generichash(	(void *) node, sizeof*(node),
				(void *) pair, sizeof*(pair),
				NULL, 0);
}

static void mtree_rebuild_node(mtree_t *mtree, node_id_t node_id, int depth)
{
	if (depth == mtree->depth)
	{
		return;
	}

	mtree_rebuild_node(mtree, mtree_child(node_id, 0), depth + 1);
	mtree_rebuild_node(mtree, mtree_child(node_id, 1), depth + 1);

	mtree_compute_node(mtree, node_id);
}

void mtree_rebuild(mtree_t *mtree)
{
	return mtree_rebuild_node(mtree, 0, 0);
}

void mtree_update_node(mtree_t *mtree, node_id_t node_id)
{
	mtree_compute_node(mtree, node_id);

	if (node_id != 0)
	{
		mtree_update_node(mtree, mtree_parent(node_id));
	}
}

void mtree_set_blk(mtree_t *mtree, blk_id_t blk_id, const blk_t *blk)
{
	node_id_t	node_id		= mtree_blk(mtree, blk_id);
	mtree_node_t (*	node)[1]	= (void *) &mtree->nodes[node_id];

	crypto_generichash(	      (void *) node, sizeof*(node),
				(const void *) blk , sizeof*(blk ),
				NULL, 0);

	if (node_id != 0)
	{
		mtree_update_node(mtree, mtree_parent(node_id));
	}
}
