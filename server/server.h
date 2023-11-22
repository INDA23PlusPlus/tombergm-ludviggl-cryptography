#ifndef SERVER_H
#define SERVER_H

#include <mtree.h>

typedef struct
{
	int		sock_fd;
	int		root_fd;
	int		data_fd;
	int		aead_fd;
	int		tree_fd;
	mtree_t *	mtree;
} server_t;

int	server_start	(server_t *sv, int sock_fd, const char *root_path);
int	server_stop	(server_t *sv);
int	server_run	(server_t *sv);

#endif
