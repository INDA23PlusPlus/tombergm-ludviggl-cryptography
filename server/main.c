#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sodium.h>
#include "server.h"
#include "err.h"

static server_t sv;

int main(int argc, char *argv[])
{
	int			ret		= EXIT_SUCCESS;
	struct protoent *	tcp		= NULL;
	int			s_sock		= -1;
	struct sockaddr_in	s_addr;
	socklen_t		s_addrlen;

	if (sodium_init() < 0)
	{
		fprintf(stderr, "error: sodium_init failed");
		ret = EXIT_FAILURE;
		goto exit;
	}

	tcp = getprotobyname("tcp");
	if (tcp == NULL)
	{
		perror("error: getprotobyname");
		ret = EXIT_FAILURE;
		goto exit;
	}

	s_sock = socket(AF_INET, SOCK_STREAM, tcp->p_proto);
	if (s_sock == -1)
	{
		perror("error: socket");
		ret = EXIT_FAILURE;
		goto exit;
	}

	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(1311);
	inet_aton("0.0.0.0", &s_addr.sin_addr);
	s_addrlen = sizeof(s_addr);

	ret = bind(s_sock, (struct sockaddr *) &s_addr, s_addrlen);
	if (ret != 0)
	{
		perror("error: bind");
		ret = EXIT_FAILURE;
		goto exit;
	}

	ret = listen(s_sock, 0);
	if (ret != 0)
	{
		perror("error: listen");
		ret = EXIT_FAILURE;
		goto exit;
	}

	for (;;)
	{
		int			c_sock;
		struct sockaddr_in	c_addr;
		socklen_t		c_addrlen;

		c_addrlen = sizeof(c_addr);
		c_sock = accept(s_sock, (struct sockaddr *) &c_addr,
				&c_addrlen);

        log("client connected\n");

		if (c_sock == -1)
		{
			perror("error: accept");
			ret = EXIT_FAILURE;
			goto exit;
		}

		ret = server_start(&sv, c_sock, "./sv_root/");
		if (ret != 0)
		{
			close(c_sock);
			ret = EXIT_FAILURE;
			goto exit;
		}

		ret = server_run(&sv);
		if (ret != 0)
		{
			server_stop(&sv);
			ret = EXIT_FAILURE;
			goto exit;
		}

        log("client disconnected\n");

		ret = server_stop(&sv);
		if (ret != 0)
		{
			ret = EXIT_FAILURE;
			goto exit;
		}
	}

	ret = EXIT_SUCCESS;

exit:
	if (s_sock != -1)
	{
		close(s_sock);
	}

	return ret;
}
