#include <stddef.h>
#include <string.h>
#include <sodium.h>
#include <blk.h>
#include <mtree.h>

static const mtree_node_t *null_blk(void)
{
	static mtree_node_t	node;
	const mtree_node_t *	ptr = NULL;

	if (ptr == NULL)
	{
		blk_t blk;

		memset(&blk, 0, sizeof(blk));

		crypto_generichash(	(void *) &node, sizeof(node),
					(void *) &blk , sizeof(blk ),
					NULL, 0);

		ptr = &node;
	}

	return ptr;
}

mtree_t *mtree_new(unsigned depth)
{
	size_t		size;
	mtree_t *	mtree;
	blk_id_t	nodes = (blk_id_t) 1 << depth;

	size = sizeof*(mtree) + sizeof*(mtree->nodes) * nodes;
	mtree = malloc(size);

	if (mtree != NULL)
	{
		mtree->depth = depth;
	}

	return mtree;
}

void mtree_update_node(mtree_t *mtree, blk_id_t node_id)
{
	mtree_node_t (*node)[1]	= (void *) &mtree->nodes[node_id];
	mtree_node_t (*pair)[2]	= (void *) &mtree->nodes[node_id << 1];

	crypto_generichash(	(void *) node, sizeof*(node),
				(void *) pair, sizeof*(pair),
				NULL, 0);

	if (node_id != 0)
	{
		mtree_update_node(mtree, mtree_parent(node_id));
	}
}

void mtree_set_blk(mtree_t *mtree, blk_id_t blk_id, const blk_t *blk)
{
	blk_id_t	node_id		= mtree_blk(mtree, blk_id);
	mtree_node_t (*	node)[1]	= (void *) &mtree->nodes[node_id];

	if (blk == NULL)
	{
		(*node)[0] = *null_blk();
	}
	else
	{
		crypto_generichash(	      (void *) node, sizeof*(node),
					(const void *) blk , sizeof*(blk ),
					NULL, 0);
	}

	if (node_id != 0)
	{
		mtree_update_node(mtree, mtree_parent(node_id));
	}
}
