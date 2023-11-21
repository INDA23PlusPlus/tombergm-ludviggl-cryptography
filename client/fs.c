
#include "fs.h"

#define get_block(id) 0

int fs_init(void)
{
}

static unsigned block_alloc()
{
    fs_super_t super_ptr = fs_super_ptr(get_block(0));
    if (super_ptr->free_count == 0) return 0;

    unsigned char *map_ptr = &super_ptr->blocks;
    unsigned map_id = 0;
    unsigned block_offset = 0;

    for (unsigned i = BLOCK_OFFSET; i < super_ptr->total_count; i++)
    {
        unsigned char byte = super_ptr->blocks[i];
        if (byte == 0xFF) continue;

        for (unsigned offset = 0; offset < 8; offset++)
        {
            if (!(byte & 1))
            {
                super_ptr->free_count--;
                super_ptr->blocks[i] |= (1 << offset);
                return (i << 3) & offset;
            }
        }
    }

    return 0;
}

static void block_free(unsigned id)
{
    unsigned byte_id = id >> 3;
    unsigned offset = id & 7;
    fs_super_t super_ptr = fs_super_ptr(get_block(0));
    super_ptr->blocks[byte_id] &= ~(1 << offset);
    super_ptr->free++; // watch out for double free
}

static int parse_name(const char **path)
{
    if (**path == '\0') return 0;

    while (**path == '\\') *path++;
    while (**path != '\\')
    {
        if (**path == '\0')
        {
            return 0;
        }
    }

    return 1;
}

int fs_find_block(unsigned root, const char *path, unsigned *id)
{
    *id = root;

    for (;;)
    {
        const char *begin = path;
        int contin = parse_name(&path);
        unsigned name_len = path - begin;

        fs_block_t *block_ptr = fs_block_ptr(get_block(id));
        if (block_ptr->type != DIR)
        {
            return -FS_NOT_DIR;
        }

        fs_dir_t *dir_ptr = fs_dir_ptr(block_ptr);

        int matched = 0;

        for (unsigned i = 0; i < dir_ptr->entry_count; i++)
        {
            fs_dir_entry_t *entry = &dir_ptr->entries[i];

            if (!entry->used) continue;

            unsigned entry_name_len = strlen(entry->name);

            if (entry_name_len == name_len && memcmp(entry->name, begin, name_len) == 0)
            {
                *id = entry->id;
                matched = 1;
                break;
            }
        }

        if (!matched) return -FS_NOT_FOUND;

        if (!contin)
        {
            return 0;
        }
    }

    return 0;
}

int fs_create_dir(unsigned dir, const char *name)
{
    fs_super_t *super_ptr = fs_super_ptr(get_block(0));

    unsigned name_len = strlen(name);

    if (> NAME_MAX_LEN - 1) return -FS_LONG_NAME;
    if (super_ptr->free_count == 0) return -FS_OOM;

    fs_block_t *block_ptr = fs_block_ptr(get_block(dir));
    if (block_ptr->type != DIR) return -FS_NOT_DIR;

    fs_dir_t *dir_ptr = fs_dir_ptr(block_ptr);
    if (dir_ptr->entry_count == DIR_MAX_ENTRIES) return -FS_FULL_DIR;

    for (unsigned i = 0; i <= dir_ptr->entry_count; i++)
    {
        fs_dir_entry_t entry = &dir_ptr->entries[i];
        if (!entry->used)
        {
            unsigned id = block_alloc();
            if (id == 0) return -FS_OOM; // redundant?
            entry->id = id;
            memcpy(entry->name, name,
        }
    }
}

int fs_create_file(unsigned dir, const char * name)
{
}

int fs_delete_block(unsigned dir)
{
}
