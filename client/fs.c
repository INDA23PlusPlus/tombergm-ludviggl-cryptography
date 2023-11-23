
#include "fs.h"
#include "cache.h"
#include "client.h"

#include <string.h>

#define SUPER_ID 0

#define get_block(id) 0

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

int fs_create_dir(client_t *cl, unsigned dir, const char *name, unsigned *id)
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
            unsigned did = block_alloc(cl);
            if (did == 0) return -FSERR_OOM;

            fs_dir_t *this_dir = verify_ptr(cache_claim_blk(cl->dir_cache, did));

            memset(this_dir, 0, sizeof*(this_dir));
            this_dir->parent      = dir;
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
            this_dir->entries[1].id = dir;

            entry->id   = did;
            entry->type = FS_DIR;
            entry->used = 1;

            memcpy(entry->name, name, name_len);

            dir_ptr->entry_count++;

            *id = did;
            cache_dirty_blk(cl->dir_cache, did);
            cache_dirty_blk(cl->dir_cache, dir);

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

            file->size     = 0;
            file->parent   = dir;
            file->entry_id = i;

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
    unsigned block_count = file_ptr->size / BLOCK_SIZE;

    for (unsigned i = 0; i < block_count; i++)
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
    for (int i = 0; i < DIR_MAX_ENTRIES; i++)
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
    if (block_free(cl, id) != 0) return FSERR_IO;

    fs_dir_t *parent = verify_ptr(cache_get_blk(cl->dir_cache, dir->parent));
    parent->entries[dir->entry_id].used = 0;
    parent->entry_count--;

    cache_dirty_blk(cl->dir_cache, dir->parent);

    return 0;
}

int fs_write_file(client_t *cl, unsigned file, const char *buf, size_t size, size_t offset)
{
    unsigned block_id;
    unsigned char *block;
    fs_file_t *file_ptr = verify_ptr(cache_get_blk(cl->dir_cache, file));

    unsigned fsbi = offset / BLOCK_SIZE;                // first sub-block id
    unsigned lsbi = (offset + size) / BLOCK_SIZE;       // last sub-block id

    unsigned fpo  = offset - fsbi * BLOCK_SIZE;         // first positions offset
                                                        //  inside first sub-block
    unsigned lpo  = offset + size - lsbi * BLOCK_SIZE;  // last positions offset
                                                        //  inside first sub-block
    unsigned bc   = file_ptr->size / BLOCK_SIZE;        // number of allocated blocks in file
    unsigned ns   = offset + size;                      // new size
    if (ns < file_ptr->size) ns = file_ptr->size;
    unsigned rbc  = ns / BLOCK_SIZE;                    // required allocated blocks

    // allocate new blocks
    for (unsigned b = bc; b < rbc; b++)
    {
        unsigned id = block_alloc(cl);
        if (id == 0) return -FSERR_OOM;
        file_ptr->blocks[b] = id;
    }
    file_ptr->size = ns;

    // buffer fits in a single block
    if (fsbi == lsbi)
    {
        block_id = file_ptr->blocks[fsbi];
        block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
        memcpy(block + fpo, buf, size);
        cache_dirty_blk(cl->reg_cache, block_id);
        return 0;
    }

    // write first block
    block_id = file_ptr->blocks[fsbi];
    block = verify_ptr(cache_get_blk(cl->reg_cache, fsbi));
    memcpy(block + fpo, buf, BLOCK_SIZE - fpo);
    cache_dirty_blk(cl->reg_cache, block_id);

    // write intermediate blocks
    for (unsigned sbi = fsbi + 1, bo = BLOCK_SIZE - fpo; sbi < lsbi; sbi++, bo += BLOCK_SIZE)
    {
        block_id = file_ptr->blocks[sbi];
        block = verify_ptr(cache_claim_blk(cl->reg_cache, fsbi));
        memcpy(block, buf + bo, BLOCK_SIZE);
        cache_dirty_blk(cl->reg_cache, block_id);
    }

    // write last block
    block_id = file_ptr->blocks[lsbi];
    block = verify_ptr(cache_get_blk(cl->reg_cache, fsbi));
    unsigned lsbo = BLOCK_SIZE * (lsbi - fsbi) - fpo;
    memcpy(block, buf + lsbo, lpo);
    cache_dirty_blk(cl->reg_cache, block_id);

    return 0;
}

int fs_read_file(client_t *cl, unsigned file, char *buf, size_t size, size_t offset)
{
    unsigned block_id;
    unsigned char *block;
    fs_file_t *file_ptr = verify_ptr(cache_get_blk(cl->dir_cache, file));

    unsigned fsbi = offset / BLOCK_SIZE;                // first sub-block id
    unsigned lsbi = (offset + size) / BLOCK_SIZE;       // last sub-block id

    unsigned fpo  = offset - fsbi * BLOCK_SIZE;         // first positions offset
                                                        //  inside first sub-block
    unsigned lpo  = offset + size - lsbi * BLOCK_SIZE;  // last positions offset
                                                        //  inside first sub-block
    unsigned bc   = file_ptr->size / BLOCK_SIZE;        // number of allocated blocks in file
    unsigned ns   = offset + size;                      // new size
    if (ns < file_ptr->size) ns = file_ptr->size;
    unsigned rbc  = ns / BLOCK_SIZE;                    // required blocks

    if (bc != rbc) return -FSERR_OVERFLOW;

    // buffer fits in a single block
    if (fsbi == lsbi)
    {
        block_id = file_ptr->blocks[fsbi];
        block = verify_ptr(cache_get_blk(cl->reg_cache, block_id));
        memcpy(buf, block + fpo, size);
        return 0;
    }

    // read first block
    block_id = file_ptr->blocks[fsbi];
    block = verify_ptr(cache_get_blk(cl->reg_cache, fsbi));
    memcpy(buf, block + fpo, BLOCK_SIZE - fpo);

    // read intermediate blocks
    for (unsigned sbi = fsbi + 1, bo = BLOCK_SIZE - fpo; sbi < lsbi; sbi++, bo += BLOCK_SIZE)
    {
        block_id = file_ptr->blocks[sbi];
        block = verify_ptr(cache_get_blk(cl->reg_cache, fsbi));
        memcpy(buf + bo, block, BLOCK_SIZE);
    }

    // read last block
    block_id = file_ptr->blocks[lsbi];
    block = verify_ptr(cache_get_blk(cl->reg_cache, fsbi));
    unsigned lsbo = BLOCK_SIZE * (lsbi - fsbi) - fpo;
    memcpy(buf + lsbo, block, lpo);

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
