#ifndef BLK_H
#define BLK_H

#include <stdint.h>
#include <sodium.h>

#define blk_crypto(d)	crypto_aead_aes256gcm ## d
#define blk_encrypt	blk_crypto(_encrypt)
#define blk_decrypt	blk_crypto(_decrypt)

#define BLK_DATA_LEN	4096
#define BLK_AUTH_LEN	blk_crypto(_ABYTES)
#define BLK_SALT_LEN	blk_crypto(_NPUBBYTES)
#define BLK_EXTR_LEN	(BLK_AUTH_LEN + BLK_SALT_LEN)

typedef uint64_t blk_id_t;

typedef struct
{
	char			data[BLK_DATA_LEN];
	union
	{
		struct
		{
			char	auth[BLK_AUTH_LEN];
			char	salt[BLK_SALT_LEN];
		};
		char		extr[BLK_EXTR_LEN];
	};
} blk_t;

#endif
