#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sodium.h>
#include <err.h>
#include "server.h"

static server_t sv;

static void usage(const char *name)
{
    fprintf(stdout,
            "Usage:\n"
            "    %s [options]\n"
            "Options:\n"
            "    --root=<dir>    Use directory <dir> for local files (default: ./sv_root/).\n"
            "    --help          Display this help message.\n"
            "\n",
            name);

    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int			ret		= EXIT_SUCCESS;
	struct protoent *	tcp		= NULL;
	int			s_sock		= -1;
	struct sockaddr_in	s_addr;
	socklen_t		s_addrlen;
	const char *		root_path	= "./sv_root/";

    if (argc == 2 && strcmp(argv[1], "--help") == 0) usage(argv[0]);

	for (int i = 1; i < argc; i++)
	{
		if (strncmp(argv[i], "--root=", 7) == 0)
		{
			root_path = &argv[i][7];
		}
		else
		{
			fprintf(stderr, "error: invalid argument: %s\n",
				argv[i]);
			return EXIT_FAILURE;
		}
	}

	if (sodium_init() < 0)
	{
		fail_fn(0, sodium_init);
	}

	tcp = try_ptr(0, getprotobyname, "tcp");

	s_sock = try_fd(0, socket, AF_INET, SOCK_STREAM, tcp->p_proto);

	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(1311);
	inet_aton("0.0.0.0", &s_addr.sin_addr);
	s_addrlen = sizeof(s_addr);

	try_fn(0, bind, s_sock, (struct sockaddr *) &s_addr, s_addrlen);
	try_fn(0, listen, s_sock, 0);

	for (;;)
	{
		int			c_sock;
		struct sockaddr_in	c_addr;
		socklen_t		c_addrlen;

		c_addrlen = sizeof(c_addr);
		c_sock = try_fd(0, accept, s_sock, (struct sockaddr *) &c_addr,
				&c_addrlen);

		log("client connected\n");

		try_fn(0, server_start, &sv, c_sock, root_path);
		try_fn(0, server_run, &sv);

		log("client disconnected\n");

		try_fn(0, server_stop, &sv);
	}

exit:
	if (ret != EXIT_SUCCESS)
	{
		ret = EXIT_FAILURE;
	}

	if (s_sock != -1)
	{
		close(s_sock);
	}

	return ret;
}
