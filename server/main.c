#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sodium.h>

int server_run(int sock);

int main(int argc, char *argv[])
{
	int			ret		= EXIT_SUCCESS;
	struct protoent *	tcp		= NULL;
	int			s_sock		= -1;
	struct sockaddr_in	s_addr;
	socklen_t		s_addrlen;
	int			c_sock		= -1;
	struct sockaddr_in	c_addr;
	socklen_t		c_addrlen;

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
		c_addrlen = sizeof(c_addr);
		c_sock = accept(s_sock, (struct sockaddr *) &c_addr,
				&c_addrlen);

		if (c_sock == -1)
		{
			perror("error: accept");
			ret = EXIT_FAILURE;
			goto exit;
		}

		ret = server_run(c_sock);
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

	if (c_sock != -1)
	{
		close(c_sock);
	}

	return ret;
}
