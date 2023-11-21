
#ifndef FS_H
#define FS_H

#define BLOCK_SIZE      4096
#define NAME_MAX_LEN    16
#define DIR_MAX_ENTRIES 128
// The offset in to memory where the usable blocks reside
#define BLOCK_OFFSET    4

enum fs_block_type { FILE, DIR, };

enum fs_error {
    FS_NOT_FOUND,
    FS_NOT_DIR,
    FS_FULL_DIR,
    FS_OOM,
    FS_LONG_NAME
};

typedef struct {
    unsigned total_count;
    unsigned free_count;
    unsigned root;
    unsigned char blocks[];
} fs_super_t;

typedef struct {
    unsigned used;
    char name[NAME_MAX_LEN];
    unsigned id;
} fs_dir_entry_t;


typedef struct {
    unsigned entry_count;
    fs_dir_entry_t entries[];
} fs_dir_t;

typedef struct {
    unsigned size;
    unsigned blocks[];
} fs_file_t;

typedef struct {
    unsigned type;
    char data[];
} fs_block_t;

#define fs_super_ptr(ptr) ((fs_super_t*) (ptr))
#define fs_block_ptr(ptr) ((fs_block_t*) (ptr))
#define fs_dir_ptr(ptr)   ((fs_dir_t*)   (((fs_block_t *) (ptr))->data))
#define fs_file_ptr(ptr)  ((fs_file_t*)  (((fs_block_t *) (ptr))->data))

int fs_init(void);
int fs_find_block(unsigned root, const char *path, unsigned *id);
int fs_create_dir(unsigned dir, const char *name);
int fs_create_file(unsigned dir, const char * name);
int fs_delete_block(unsigned dir);

#endif
