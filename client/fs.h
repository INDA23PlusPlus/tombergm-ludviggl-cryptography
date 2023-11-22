
#ifndef FS_H
#define FS_H

#include "cache.h"
#include "blk.h"

#define BLOCK_SIZE      BLK_DATA_LEN
#define NAME_MAX_LEN    16
#define DIR_MAX_ENTRIES 128
// The offset in to memory where the usable blocks reside
#define BLOCK_OFFSET    4

enum fs_block_type { FS_FILE, FS_DIR, };

enum fs_error {
    FSERR_OK = 0,
    FSERR_NOT_FOUND,
    FSERR_NOT_DIR,
    FSERR_FULL_DIR,
    FSERR_OOM,
    FSERR_LONG_NAME,
    FSERR_IO,
    FSERR_OVERFLOW
};

typedef struct {
    unsigned total_count;
    unsigned free_count;
    unsigned root;
    unsigned map_count;
} fs_super_t;

typedef struct {
    unsigned used;
    unsigned type;
    char     name[NAME_MAX_LEN];
    unsigned id;
} fs_dir_entry_t;


typedef struct {
    unsigned       parent;
    unsigned       entry_id;
    unsigned       entry_count;
    fs_dir_entry_t entries[];
} fs_dir_t;

typedef struct {
    unsigned parent;
    unsigned entry_id;
    unsigned size;
    unsigned blocks[];
} fs_file_t;

int fs_init(client_t *cl, unsigned max_blocks);
int fs_find_block(client_t *cl, unsigned root, const char *path, unsigned *id, unsigned *type);
int fs_create_dir(client_t *cl, unsigned dir, const char *name, unsigned *id);
int fs_create_file(client_t *cl, unsigned dir, const char *name, unsigned *id);
int fs_delete_block(client_t *cl, unsigned dir);
int fs_write_file(client_t *cl, unsigned file, const char *buf, size_t size, size_t offset);
int fs_read_file(client_t *cl, unsigned file, char *buf, size_t size, size_t offset);
int fs_get_file_size(client_t *cl, unsigned id, unsigned *size);
unsigned fs_get_root(client_t *cl);

#endif
