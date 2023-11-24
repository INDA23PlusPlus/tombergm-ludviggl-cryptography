#include <string.h>
#include <time.h>
#include "cache.h"
#include "client.h"
#include "err.h"
#include "fs.h"

#define SUPER_ID 0

#define verify_ptr(ptr)\
    ({\
        void *__ptr = ptr;\
        if (__ptr == NULL) return -FSERR_IO;\
        __ptr;\
    })

static unsigned block_alloc(client_t *cl)
{
    // TODO: Make super parameter
    fs_super_t *super = verify_ptr(cache_get_blk(cl->sb_cache, SUPER_ID));

    unsigned map_count = super->map_count;
    unsigned map_id    = 1;
    unsigned char *map = verify_ptr(cache_get_blk(cl->sb_cache, map_id));

    for (unsigned byte_id = map_count / 8 + 1; byte_id < BLOCK_SIZE * map_count; byte_id++)
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
                cache_dirty_blk(cl->sb_cache, map_id);
                return byte_id * 8 + offset;
            }
        }
    }

    return 0;
}

static int block_free(client_t *cl, unsigned id)
{
    unsigned byte_id     = id / 8;
    unsigned byte_offset = id % 8;
    unsigned map_id      = 1 + byte_id / BLOCK_SIZE;
    unsigned map_offset  = byte_id % BLOCK_SIZE;

    unsigned char *map = verify_ptr(cache_get_blk(cl->sb_cache, map_id));

    map[map_offset] &= ~(1 << byte_offset);
    cache_dirty_blk(cl->sb_cache, map_id);

    return 0;
}

int fs_init(client_t *cl, unsigned map_count)
{
    fs_super_t *super = verify_ptr(cache_claim_blk(cl->sb_cache, SUPER_ID));

    super->total_count = map_count * BLOCK_SIZE * 8;
    super->free_count  = super->total_count;
    super->map_count   = map_count;

    unsigned root_id = block_alloc(cl);
    fs_dir_t *root   = verify_ptr(cache_claim_blk(cl->dir_cache, root_id));

    memset(root, 0, BLOCK_SIZE);

    root->entry_count = 0;
    super->root = root_id;

    timespec_get(&root->acc, TIME_UTC);
    timespec_get(&root->mod, TIME_UTC);

    cache_dirty_blk(cl->sb_cache, SUPER_ID);
    cache_dirty_blk(cl->dir_cache, root_id);

    return 0;
}

static int parse_name(const char **begin, const char **path)
{
    while (**path == '/') (*path)++;
    if (**path == '\0') return 0;

    *begin = *path;

    while (**path != '/')
    {
        if (**path == '\0')
        {
            return 1;
        }
        (*path)++;
    }

    return 1;
}

int fs_find_block(client_t *cl, unsigned root, const char *path, unsigned *id, unsigned *type)
{
    fs_super_t *super = verify_ptr(cache_get_blk(cl->sb_cache, SUPER_ID));
    *id = super->root;
    *type = FS_DIR;
    fs_dir_t *dir = verify_ptr(cache_get_blk(cl->dir_cache, *id));

    const char *begin = path;

    for (;;)
    {
        if (!parse_name(&begin, &path)) break;

        unsigned name_len = path - begin;
        int matched = 0;

        for (unsigned i = 0; i < dir->entry_count; i++)
        {
            fs_dir_entry_t *entry = &dir->entries[i];

            if (!entry->used) continue;

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
    }

    return 0;
}

int fs_create_dir(client_t *cl, unsigned parent, const char *name, unsigned *id)
{
    fs_super_t *super_ptr = verify_ptr(cache_get_blk(cl->sb_cache, SUPER_ID));

    unsigned name_len = strlen(name) + 1; // include \0

    if (name_len > NAME_MAX_LEN) return -FSERR_LONG_NAME;
    if (super_ptr->free_count == 0) return -FSERR_OOM;

    fs_dir_t *dir_ptr = verify_ptr(cache_get_blk(cl->dir_cache, parent));
    if (dir_ptr->entry_count == DIR_MAX_ENTRIES) return -FSERR_FULL_DIR;

    for (unsigned i = 0; i <= dir_ptr->entry_count; i++)
    {
        fs_dir_entry_t *entry = &dir_ptr->entries[i];
        if (!entry->used)
        {
            unsigned did = block_alloc(cl);
            if (did == 0) return -FSERR_OOM;

            fs_dir_t *this_dir = verify_ptr(cache_claim_blk(cl->dir_cache, did));

            memset(this_dir, 0, sizeof*(this_dir));
            this_dir->parent      = parent;
            this_dir->entry_id    = i;
            this_dir->entry_count = 2;

            // add . and .. entries
            this_dir->entries[0].used = 1;
            this_dir->entries[0].type = FS_DIR;
            memcpy(this_dir->entries[0].name, ".", 2);
            this_dir->entries[0].id = did;

            this_dir->entries[1].used = 1;
            this_dir->entries[1].type = FS_DIR;
            memcpy(this_dir->entries[1].name, "..", 3);
            this_dir->entries[1].id = parent;

            entry->id   = did;
            entry->type = FS_DIR;
            entry->used = 1;

            memcpy(entry->name, name, name_len);

            dir_ptr->entry_count++;

            *id = did;
            cache_dirty_blk(cl->dir_cache, did);
            cache_dirty_blk(cl->dir_cache, parent);

            return 0;
        }
    }

    return 0;
}

int fs_create_file(client_t *cl, unsigned dir, const char *name, unsigned *id)
{
    fs_super_t *super_ptr = verify_ptr(cache_get_blk(cl->sb_cache, SUPER_ID));

    unsigned name_len = strlen(name) + 1; // include \0

    if (name_len > NAME_MAX_LEN) return -FSERR_LONG_NAME;
    if (super_ptr->free_count == 0) return -FSERR_OOM;

    fs_dir_t *dir_ptr = verify_ptr(cache_get_blk(cl->dir_cache, dir));
    if (dir_ptr->entry_count == DIR_MAX_ENTRIES) return -FSERR_FULL_DIR;

    for (unsigned i = 0; i <= dir_ptr->entry_count; i++)
    {
        fs_dir_entry_t *entry = &dir_ptr->entries[i];
        if (!entry->used)
        {
            unsigned fid = block_alloc(cl);
            if (fid == 0) return -FSERR_OOM;

            fs_file_t *file = verify_ptr(cache_claim_blk(cl->dir_cache, fid));

            file->size        = 0;
            file->block_count = 0;
            file->parent      = dir;
            file->entry_id    = i;

            entry->id = fid;
            entry->type = FS_FILE;
            entry->used = 1;
            memcpy(entry->name, name, name_len);

            dir_ptr->entry_count++;

            *id = fid;
            cache_dirty_blk(cl->dir_cache, dir);
            cache_dirty_blk(cl->dir_cache, fid);

            return 0;
        }
    }

    return 0;
}

int fs_delete_file(client_t *cl, unsigned id)
{
    fs_file_t *file_ptr = verify_ptr(cache_get_blk(cl->dir_cache, id));

    for (unsigned i = 0; i < file_ptr->block_count; i++)
    {
        if (block_free(cl, file_ptr->blocks[id]) != 0) return FSERR_IO;
    }

    fs_dir_t *parent = verify_ptr(cache_get_blk(cl->dir_cache, file_ptr->parent));
    parent->entries[file_ptr->entry_id].used = 0;
    parent->entry_count--;

    cache_dirty_blk(cl->dir_cache, file_ptr->parent);

    if (block_free(cl, id) != 0) return FSERR_IO;

    return 0;
}

int fs_delete_dir(client_t *cl, unsigned id)
{
    fs_dir_t *dir = verify_ptr(cache_get_blk(cl->dir_cache, id));

    // start from 2 because '.' & '..'
    for (int i = 2; i < DIR_MAX_ENTRIES; i++)
    {
        fs_dir_entry_t *entry = &dir->entries[i];
        if (entry->used)
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
    if (block_free(cl, id) != 0) return -FSERR_IO;

    fs_dir_t *parent = verify_ptr(cache_get_blk(cl->dir_cache, dir->parent));
    parent->entries[dir->entry_id].used = 0;
    parent->entry_count--;

    cache_dirty_blk(cl->dir_cache, dir->parent);

    return 0;
}

int fs_write_file(client_t *cl, unsigned file, const char *buf, size_t size, size_t offset, size_t *bytes_written)
{
    unsigned char *block;
    unsigned block_id;

    fs_file_t *fptr = verify_ptr(cache_get_blk(cl->dir_cache, file));
    *bytes_written = 0;

    if (size == 0) return 0;

    cache_dirty_blk(cl->dir_cache, file);

    unsigned stop_pos     = size + offset;
    unsigned start_offset = offset % BLOCK_SIZE;
    unsigned first_block  = offset / BLOCK_SIZE;
    unsigned stop_offset;
    unsigned last_block;

    if (stop_pos % BLOCK_SIZE == 0)
    {
        stop_offset = BLOCK_SIZE;
        last_block  = stop_pos / BLOCK_SIZE - 1;
    }
    else
    {
        stop_offset = stop_pos % BLOCK_SIZE;
        last_block  = stop_pos / BLOCK_SIZE;
    }

    if (last_block >= fptr->block_count)
    {
        for (unsigned i = fptr->block_count; i <= last_block; i++)
        {
            block_id = block_alloc(cl);
            if (block_id == 0) return -FSERR_OOM;
            fptr->blocks[i] = block_id;
            fptr->block_count++;
        }
    }

    if (first_block == last_block)
    {
        block_id = fptr->blocks[first_block];
        block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
        memcpy(block + start_offset, buf + *bytes_written, stop_offset - start_offset);
        cache_dirty_blk(cl->reg_cache, block_id);
        *bytes_written += stop_offset - start_offset;
        if (offset + *bytes_written > fptr->size) fptr->size = offset + *bytes_written;
        return 0;
    }

    block_id = fptr->blocks[first_block];
    block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
    memcpy(block + start_offset, buf + *bytes_written, BLOCK_SIZE - start_offset);
    cache_dirty_blk(cl->reg_cache, block_id);
    *bytes_written += BLOCK_SIZE - start_offset;
    if (offset + *bytes_written > fptr->size) fptr->size = offset + *bytes_written;

    for (unsigned i = first_block + 1; i < last_block; i++)
    {
        block_id = fptr->blocks[i];
        block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
        memcpy(block, buf + *bytes_written, BLOCK_SIZE);
        cache_dirty_blk(cl->reg_cache, block_id);
        *bytes_written += BLOCK_SIZE;
        if (offset + *bytes_written > fptr->size) fptr->size = offset + *bytes_written;
    }

    block_id = fptr->blocks[last_block];
    block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
    memcpy(block, buf + *bytes_written, stop_offset);
    cache_dirty_blk(cl->reg_cache, block_id);
    *bytes_written += stop_offset;
    if (offset + *bytes_written > fptr->size) fptr->size = offset + *bytes_written;

    return 0;
}

int fs_read_file(client_t *cl, unsigned file, char *buf, size_t size, size_t offset, size_t *bytes_read)
{
    unsigned char *block;
    unsigned block_id;

    fs_file_t *fptr = verify_ptr(cache_get_blk(cl->dir_cache, file));
    *bytes_read = 0;

    if (size == 0) return 0;
    if (offset >= fptr->size) return 0;

    unsigned stop_pos     = size + offset;
    if (stop_pos > fptr->size) stop_pos = fptr->size;
    unsigned start_offset = offset % BLOCK_SIZE;
    unsigned first_block  = offset / BLOCK_SIZE;
    unsigned stop_offset;
    unsigned last_block;

    if (stop_pos % BLOCK_SIZE == 0)
    {
        stop_offset = BLOCK_SIZE;
        last_block  = stop_pos / BLOCK_SIZE - 1;
    }
    else
    {
        stop_offset = stop_pos % BLOCK_SIZE;
        last_block  = stop_pos / BLOCK_SIZE;
    }

    if (first_block >= fptr->block_count) return 0;

    if (first_block == last_block)
    {
        block_id = fptr->blocks[first_block];
        block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
        memcpy(buf + *bytes_read, block + start_offset, stop_offset - start_offset);
        *bytes_read += stop_offset - start_offset;
        return 0;
    }

    block_id = fptr->blocks[first_block];
    block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
    memcpy(buf + *bytes_read, block + start_offset, BLOCK_SIZE - start_offset);
    *bytes_read += BLOCK_SIZE - start_offset;

    for (unsigned i = first_block + 1; i < last_block; i++)
    {
        block_id = fptr->blocks[i];
        block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
        memcpy(buf + *bytes_read, block, BLOCK_SIZE);
        *bytes_read += BLOCK_SIZE;
    }

    block_id = fptr->blocks[last_block];
    block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
    memcpy(buf + *bytes_read, block, stop_offset);
    *bytes_read += stop_offset;

    return 0;
}

int fs_truncate_file(client_t *cl, unsigned id, unsigned size)
{
    fs_file_t *fptr = verify_ptr(cache_get_blk(cl->dir_cache, id));

    unsigned new_block_count;
    unsigned block_id;

    if (size % BLOCK_SIZE == 0)
    {
        new_block_count = size / BLOCK_SIZE;
    }
    else
    {
        new_block_count = size / BLOCK_SIZE + 1;
    }

    if (fptr->block_count < new_block_count)
    {
        for (unsigned i = fptr->block_count; i < new_block_count; i++)
        {
            block_id = block_alloc(cl);
            if (block_id == 0) return -FSERR_OOM;
            fptr->blocks[i] = block_id;
        }
    }
    else
    if (fptr->block_count > new_block_count)
    {
        for (unsigned i = new_block_count; i < fptr->block_count; i++)
        {
            unsigned ret = block_free(cl, fptr->blocks[i]);
            if (ret != 0) return ret;
        }
    }

    fptr->size = size;
    fptr->block_count = new_block_count;

    return 0;
}

unsigned fs_get_root(client_t *cl)
{
    fs_super_t *super = cache_get_blk(cl->sb_cache, SUPER_ID);
    if (super == NULL) return 0;
    return super->root;
}

int fs_get_file_size(client_t *cl, unsigned id, unsigned *size)
{
    fs_file_t *file = verify_ptr(cache_get_blk(cl->dir_cache, id));
    *size = file->size;
    return 0;
}
