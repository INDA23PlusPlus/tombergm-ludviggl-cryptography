#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sodium.h>
#include <blk.h>
#include <cmd.h>

#define KEY_LEN	blk_crypto(_KEYBYTES)

static int			c_sock		= -1;
static struct sockaddr_in	c_addr;
static socklen_t		c_addrlen;

static char			c_key[KEY_LEN];
static char			c_salt[BLK_SALT_LEN];

int client_start(void)
{
	int			ret		= 0;
	struct protoent *	tcp		= NULL;

	if (sodium_init() < 0)
	{
		fprintf(stderr, "error: sodium_init failed");
		ret = -1;
		goto exit;
	}

	tcp = getprotobyname("tcp");
	if (tcp == NULL)
	{
		perror("error: getprotobyname");
		ret =  -1;
		goto exit;
	}

	c_sock = socket(AF_INET, SOCK_STREAM, tcp->p_proto);
	if (c_sock == -1)
	{
		perror("error: socket");
		ret =  -1;
		goto exit;
	}

	c_addr.sin_family = AF_INET;
	c_addr.sin_port = htons(1311);
	inet_aton("127.0.0.1", &c_addr.sin_addr);
	c_addrlen = sizeof(c_addr);

	ret = connect(c_sock, (struct sockaddr *) &c_addr, c_addrlen);
	if (ret != 0)
	{
		perror("error: connect");
		ret =  -1;
		goto exit;
	}

	char pw[] = "password123";
	char salt[crypto_pwhash_SALTBYTES] = { 0 };

	ret = crypto_pwhash(	(void *) c_key, sizeof(c_key),
				(void *) pw   , sizeof(pw   ),
				(void *) salt ,
				crypto_pwhash_OPSLIMIT_INTERACTIVE,
				crypto_pwhash_MEMLIMIT_INTERACTIVE,
				crypto_pwhash_ALG_DEFAULT);
	if (ret != 0)
	{
		errno = ENOMEM;
		perror("error: crypto_pwhash");
		ret =  -1;
		goto exit;
	}

	randombytes_buf(c_salt, sizeof(c_salt));

	ret = 0;

exit:
	if (ret != 0)
	{
		if (c_sock != -1)
		{
			close(c_sock);
		}
	}

	return ret;
}

int client_stop(void)
{
	if (c_sock != -1)
	{
		close(c_sock);
	}

	return 0;
}

int client_read_blk(blk_t *blk, blk_id_t id)
{
	int ret;

	cmd_t cmd = CMD_RD_BLK;

	ret = send(c_sock, &cmd, sizeof(cmd), MSG_MORE);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		return -1;
	}

	ret = send(c_sock, &id, sizeof(id), MSG_MORE);
	if (ret != sizeof(id))
	{
		perror("error: send");
		return -1;
	}

	ret = recv(c_sock, blk, sizeof*(blk), MSG_WAITALL);
	if (ret != sizeof*(blk))
	{
		perror("error: recv");
		return -1;
	}

	ret = blk_decrypt(
		(void *) blk->data, NULL,
		NULL,
		(void *) blk->data, sizeof(blk->data) + sizeof(blk->auth),
		NULL, 0,
		(void *) blk->salt,
		(void *) c_key);

	return 0;
}

int client_write_blk(blk_t *blk, blk_id_t id)
{
	int ret;

	cmd_t cmd = CMD_WR_BLK;

	ret = send(c_sock, &cmd, sizeof(cmd), MSG_MORE);
	if (ret != sizeof(cmd))
	{
		perror("error: send");
		return -1;
	}

	ret = send(c_sock, &id, sizeof(id), MSG_MORE);
	if (ret != sizeof(id))
	{
		perror("error: send");
		return -1;
	}

	memcpy(blk->salt, c_salt, sizeof(c_salt));

	blk_encrypt(
		(void *) blk->data, NULL,
		(void *) blk->data, sizeof(blk->data),
		NULL, 0,
		NULL,
		(void *) blk->salt,
		(void *) c_key);

	ret = send(c_sock, blk, sizeof*(blk), 0);
	if (ret != sizeof*(blk))
	{
		perror("error: send");
		return -1;
	}

	return 0;
}
