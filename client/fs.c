
#include "fs.h"
#include "cache.h"
#include "client.h"

#include <string.h>

#define get_block(id) 0

#define verify_ptr(ptr)\
    ({\
        void *__ptr = ptr;\
        if (__ptr == NULL) return -FSERR_IO;\
        __ptr;\
    })

int fs_init(client_t *cl, unsigned map_count)
{
    fs_super_t *super = verify_ptr(cache_get_blk(cl->sb_cache, 0));

    super->total_count = map_count * BLOCK_SIZE * 8;
    super->free_count  = super->total_count;
    super->map_count   = map_count;

    unsigned root_id = block_alloc(cl);
    fs_dir_t *root   = verify_ptr(cache_get_blk(cl->dir_cache, root_id));

    memset(root, 0, BLOCK_SIZE);

    root->free_count = DIR_MAX_ENTRIES;
    entry_count      = 0;

    super->root = root;

    return 0;
}

static unsigned block_alloc(client_t *cl)
{
    fs_super_t *super = verify_ptr(cache_get_blk(cl->sb_cache, 0));

    unsigned map_count = super->map_count;
    unsigned map_id    = 1;
    unsigned char *map = verify_ptr(cache_get_blk(cl->sb_cache, map_id));

    for (unsigned byte_id = map_count; byte_id < BLOCK_SIZE * map_count; byte_id++)
    {
        map_id = byte_id / BLOCK_SIZE;
        if (byte_id % BLOCK_SIZE == 0)
        {
            map = verify_ptr(cache_get_blk(cl->sb_cache, map_id));
        }

        unsigned char *byte = &map[byte_id % BLOCK_SIZE];
        if (*byte == 0xFF) continue;

        for (unsigned offset = 0; offset < 8; offset++)
        {
            if (!((*byte >> offset) & 1))
            {
                *byte |= (1 << offset);
                return byte_id * 8 + offset;
            }
        }
    }

    return 0;
}

static int block_free(client_t *cl, unsigned id)
{
    fs_super_t *super = verify_ptr(cache_get_blk(&cl->sb_cache, 0));

    unsigned byte_id     = id / 8;
    unsigned byte_offset = id % 8;
    unsigned map_id      = 1 + byte_id / BLOCK_SIZE;
    unsigned map_offset  = byte_id % BLOCK_SIZE;

    unsigned char *map = verify_ptr(cache_get_blk(&cl->sb_cache, map_id));

    map[map_offset] &= ~(1 << byte_offset);

    return 0;
}

static int parse_name(const char **path)
{
    if (**path == '\0') return 0;

    while (**path == '/') (*path)++;
    while (**path != '/')
    {
        if (**path == '\0')
        {
            return 0;
        }
    }

    return 1;
}

int fs_find_block(client_t *cl, unsigned root, const char *path, unsigned *id, unsigned *type)
{
    fs_super_t *super = verify_ptr(cache_get_blk(cl->sb_cache, 0));
    *id = super->root;
    fs_dir_t *dir = verify_ptr(cache_get_blk(cl->dir_cache, *id));

    for (;;)
    {
        const char *begin = path;
        int contin = parse_name(&path);
        unsigned name_len = path - begin;

        int matched = 0;

        for (unsigned i = 0; i < dir->entry_count; i++)
        {
            fs_dir_entry_t *entry = &dir->entries[i];

            if (entry->free) continue;

            unsigned entry_name_len = strlen(entry->name);

            if (entry_name_len == name_len && memcmp(entry->name, begin, name_len) == 0)
            {
                *id = entry->id;
                *type = entry->type;
                if (*type == FS_FILE)
                {
                    return 0;
                }
                dir = verify_ptr(cache_get_blk(cl->dir_cache, *id));
                matched = 1;
                break;
            }
        }

        if (!matched) return -FSERR_NOT_FOUND;

        if (!contin)
        {
            return 0;
        }
    }

    return 0;
}

int fs_create_dir(client_t *cl, unsigned dir, const char *name)
{
    fs_super_t *super_ptr = verify_ptr(cache_get_blk(cl->sb_cache, 0));

    unsigned name_len = strlen(name) + 1; // include \0

    if (name_len > NAME_MAX_LEN) return -FSERR_LONG_NAME;
    if (super_ptr->free_count == 0) return -FSERR_OOM;

    fs_dir_t *dir_ptr = verify_ptr(cache_get_blk(cl->dir_cache, dir));
    if (dir_ptr->entry_count == DIR_MAX_ENTRIES) return -FSERR_FULL_DIR;

    for (unsigned i = 0; i <= dir_ptr->entry_count; i++)
    {
        fs_dir_entry_t *entry = &dir_ptr->entries[i];
        if (entry->free)
        {
            unsigned id = block_alloc(cl);
            if (id == 0) return -FSERR_OOM;

            fs_dir_t *this_dir = verify_ptr(cache_get_blk(cl->dir_cache, id));

            memset(this_dir, 0, sizeof*(this_dir));
            this_dir->parent     = dir;
            this_dir->entry_id   = i;
            this_dir->free_count = DIR_MAX_ENTRIES;

            entry->id   = id;
            entry->type = FS_DIR;

            memcpy(entry->name, name, name_len);

            dir_ptr->free_count--;
            return 0;
        }
    }

    return 0;
}

int fs_create_file(client_t *cl, unsigned dir, const char *name)
{
    fs_super_t *super_ptr = verify_ptr(cache_get_blk(cl->sb_cache, 0));

    unsigned name_len = strlen(name) + 1; // include \0

    if (name_len > NAME_MAX_LEN) return -FSERR_LONG_NAME;
    if (super_ptr->free_count == 0) return -FSERR_OOM;

    fs_dir_t *dir_ptr = verify_ptr(cache_get_blk(cl->dir_cache, dir));
    if (dir_ptr->entry_count == DIR_MAX_ENTRIES) return -FSERR_FULL_DIR;

    for (unsigned i = 0; i <= dir_ptr->entry_count; i++)
    {
        fs_dir_entry_t *entry = &dir_ptr->entries[i];
        if (entry->free)
        {
            unsigned id = block_alloc(cl);
            if (id == 0) return -FSERR_OOM;

            fs_file_t *file = verify_ptr(cache_get_blk(cl->dir_cache, id));

            file->parent   = dir;
            file->entry_id = i;

            entry->id = id;
            entry->type = FS_FILE;
            memcpy(entry->name, name, name_len);

            dir_ptr->free_count--;

            return 0;
        }
    }

    return 0;
}

int fs_delete_file(client_t *cl, unsigned id)
{
    fs_file_t *file_ptr = verify_ptr(cache_get_blk(cl->dir_cache, id));
    unsigned block_count = file_ptr->size / BLOCK_SIZE;

    for (unsigned i = 0; i < block_count; i++)
    {
        if (block_free(cl, file_ptr->blocks[id]) != 0) return FSERR_IO;
    }

    fs_dir_t *parent = verify_ptr(cache_get_blk(cl->dir_cache, file_ptr->parent));
    parent->entries[file_ptr->entry_id].free = 1;
    parent->free_count++;
    if (block_free(cl, id) != 0) return FSERR_IO;

    return 0;
}

int fs_delete_dir(client_t *cl, unsigned id)
{
    fs_dir_t *dir = verify_ptr(cache_get_blk(cl->dir_cache, id));
    for (int i = 0; i < DIR_MAX_ENTRIES; i++)
    {
        fs_dir_entry_t *entry = &dir->entries[i];
        if (!entry->free)
        {
            switch (entry->type)
            {
                case FS_FILE:
                    fs_delete_file(cl, entry->id);
                    break;
                case FS_DIR:
                    fs_delete_dir(cl, entry->id);
                    break;
                default:
                    break;
            }
        }
    }

    if (block_free(cl, id) != 0) return FSERR_IO;

    return 0;
}

int fs_write_file(client_t *cl, unsigned file, const char *buf, size_t size, size_t offset)
{
    char *block;

    fs_file_t *file_ptr = verify_ptr(cache_get_blk(cl->dir_cache, file));

    unsigned fsize       = file_ptr->size;
    unsigned end_offset  = offset + size;
    unsigned new_size    = end_offset > fsize ? end_offset : fsize;
    unsigned block_count = fsize / BLOCK_SIZE;

    block = verify_ptr(cache_get_blk(cl->reg_cache, file_ptr->blocks[offset / BLOCK_SIZE]));

    for (size_t fpos = offset, bpos = 0; fpos < size + offset; fpos++, bpos++)
    {
        unsigned block_ms = fpos / BLOCK_SIZE;
        unsigned block_ls = fpos % BLOCK_SIZE;

        if (block_ms > block_count)
        {
            unsigned new_block_id = block_alloc(cl);
            if (new_block_id == 0) return -FSERR_OOM;

            block_count++;
            file_ptr->blocks[block_ms] = new_block_id;
        }

        if (block_ls == 0)
        {
            block = verify_ptr(cache_get_blk(cl->reg_cache, file_ptr->blocks[block_ms]));
        }

        block[block_ls] = buf[bpos];
    }

    file_ptr->size = new_size;

    return 0;
}

int fs_read_file(client_t *cl, unsigned file, char *buf, size_t size, size_t offset)
{
    char *block;
    fs_file_t *file_ptr = verify_ptr(cache_get_blk(cl->dir_cache, file));

    if (offset + size > file_ptr->size) return -FSERR_OVERFLOW;

    block = verify_ptr(cache_get_blk(cl->reg_cache, file_ptr->blocks[offset / BLOCK_SIZE]));

    for (size_t fpos = offset, bpos = 0; fpos < size + offset; fpos++, bpos++)
    {
        unsigned block_ms = fpos / BLOCK_SIZE;
        unsigned block_ls = fpos % BLOCK_SIZE;

        if (block_ls == 0)
        {
            block = verify_ptr(cache_get_blk(cl->reg_cache, file_ptr->blocks[block_ms]));
        }

        buf[bpos] = block[block_ls];
    }

    return 0;
}
