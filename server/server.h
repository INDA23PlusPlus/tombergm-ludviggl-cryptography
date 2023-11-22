#ifndef SERVER_H
#define SERVER_H

#include <mtree.h>

typedef struct
{
	int		sock;
	mtree_t *	mtree;
} server_t;

int	server_start	(server_t *sv, int sock);
int	server_stop	(server_t *sv);
int	server_run	(server_t *sv);

#endif
