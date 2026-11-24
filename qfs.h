/*
** qfs.h -- Data structures and helper functions for QFS version 1
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Every program in this project includes this header. It has three parts:
**
**   1. Constants taken directly from the QFS specification.
**   2. The two on-disk structures (superblock and directory entry),
**      declared exactly as the specification lays them out.
**   3. Small helper functions that every program needs: computing where
**      a block starts, reading and writing whole blocks, getting and
**      setting a block's next-block field, and finding a file by name.
**
** The helpers are "static inline" so that this header can be included by
** several programs that are each built from a single .c file. "static"
** keeps each copy private to the program that includes it, and "inline"
** stops gcc from warning about helpers a particular program doesn't use.
*/

#ifndef QFS_H
#define QFS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. Constants from the specification                                 */
/* ------------------------------------------------------------------ */

#define QFS_MAGIC          0x51      /* fs_type of every QFS image ('Q') */
#define QFS_VERSION        1         /* value mkfs writes to version     */

#define QFS_DIR_OFFSET     32        /* the directory follows superblock */
#define QFS_NUM_ENTRIES    255       /* entries in the directory table   */
#define QFS_DATA_OFFSET    8192      /* block 0 starts here              */

#define QFS_MAX_IMAGE      125829120L /* 120 MiB: largest legal image    */

#define QFS_BLOCK_FREE     0         /* flag byte of a free block        */
#define QFS_BLOCK_BUSY     1         /* flag byte of a block in use      */
#define QFS_END_OF_CHAIN   0xFFFF    /* next-block value of a last block */
                                     /* and starting block of empty file */

#define QFS_MAX_NAME       22        /* longest file name (plus a NUL)   */
#define QFS_MAX_LABEL      14        /* longest volume label (plus NUL)  */

#define QFS_DEFAULT_PERMS  0x17      /* owner rw, group r, world r       */

/* ------------------------------------------------------------------ */
/* 2. On-disk structures                                               */
/* ------------------------------------------------------------------ */

/*
** #pragma pack(push, 1) tells gcc not to insert padding between fields.
** Without it, gcc would put a padding byte after fs_type so that
** total_blocks starts on an even address, the superblock would be 34
** bytes, and every field after the first would be read from the wrong
** place. The _Static_assert lines below make the build fail if the sizes
** are ever wrong, rather than letting the programs silently misread
** images.
**
** All multi-byte fields are little-endian on disk. The course VMs are
** little-endian too, so fread() straight into these structures gives the
** right values.
*/
#pragma pack(push, 1)

typedef struct {
    uint8_t  fs_type;              /* magic number: 0x51 ('Q')            */
    uint16_t total_blocks;         /* number of data blocks               */
    uint16_t available_blocks;     /* number of free data blocks          */
    uint16_t bytes_per_block;      /* 512, 1024, or 2048                  */
    uint8_t  total_direntries;     /* always 255                          */
    uint8_t  available_direntries; /* number of free directory entries    */
    uint8_t  version;              /* format version: 1 (0 also means 1)  */
    uint8_t  reserved[7];          /* all 0                               */
    char     label[15];            /* volume label, NUL-terminated        */
} qfs_superblock;

typedef struct {
    char     filename[23];         /* NUL-terminated; "" = free entry     */
    uint8_t  permissions;          /* permission and type bits            */
    uint8_t  owner_id;             /* owner (0-255)                       */
    uint8_t  group_id;             /* group (0-255)                       */
    uint16_t starting_block;       /* first block, or 0xFFFF if empty     */
    uint32_t file_size;            /* size of the file in bytes           */
} qfs_direntry;

#pragma pack(pop)

_Static_assert(sizeof(qfs_superblock) == 32, "superblock must be 32 bytes");
_Static_assert(sizeof(qfs_direntry) == 32, "directory entry must be 32 bytes");

/*
** There is deliberately no structure for a data block. A block's
** next-block field sits at offset B - 2, where B is the block size, so
** its position isn't fixed at compile time. Programs read a whole block
** into an array of B bytes and use these offsets instead:
**
**     buf[0]               flag byte (1 = busy, 0 = free)
**     buf + 1              data: B - 3 bytes
**     buf[B-2], buf[B-1]   next block number, little-endian
*/

/* ------------------------------------------------------------------ */
/* 3. Helper functions                                                 */
/* ------------------------------------------------------------------ */

/* Byte offset in the image where block b starts. */
static inline long qfs_block_offset(const qfs_superblock *sb, uint16_t b)
{
    return QFS_DATA_OFFSET + (long)b * sb->bytes_per_block;
}

/* Byte offset in the image of directory entry i (0 <= i < 255). */
static inline long qfs_entry_offset(int i)
{
    return QFS_DIR_OFFSET + (long)i * sizeof(qfs_direntry);
}

/* How many bytes of file data one block holds: B - 3. */
static inline uint32_t qfs_data_per_block(const qfs_superblock *sb)
{
    return (uint32_t)sb->bytes_per_block - 3;
}

/*
** How many blocks a file of `size` bytes needs: size / (B - 3), rounded
** up. Adding (per - 1) before dividing is the usual integer trick for
** rounding up. An empty file needs no blocks.
*/
static inline uint32_t qfs_blocks_needed(const qfs_superblock *sb,
                                         uint32_t size)
{
    uint32_t per = qfs_data_per_block(sb);
    return (size + per - 1) / per;
}

/*
** Get and set a block's next-block field. The two bytes are combined by
** hand, low byte first, so the code says exactly what the specification
** says ("little-endian") and works on any machine.
*/
static inline uint16_t qfs_get_next(const uint8_t *buf, uint16_t B)
{
    return (uint16_t)(buf[B - 2] | (buf[B - 1] << 8));
}

static inline void qfs_set_next(uint8_t *buf, uint16_t B, uint16_t next)
{
    buf[B - 2] = next & 0xFF;          /* low byte first  */
    buf[B - 1] = (next >> 8) & 0xFF;   /* then high byte  */
}

/*
** Read the superblock from an open image and check that it is a QFS
** version 1 image. Prints a message and returns -1 if it isn't.
*/
static inline int qfs_read_superblock(FILE *fp, qfs_superblock *sb)
{
    if (fseek(fp, 0, SEEK_SET) != 0 ||
        fread(sb, sizeof(*sb), 1, fp) != 1) {
        fprintf(stderr, "Error: can't read the superblock\n");
        return -1;
    }
    if (sb->fs_type != QFS_MAGIC) {
        fprintf(stderr, "Error: not a QFS image (fs_type is 0x%02X)\n",
                sb->fs_type);
        return -1;
    }
    if (sb->version > QFS_VERSION) {
        fprintf(stderr, "Error: QFS version %u is not supported\n",
                sb->version);
        return -1;
    }
    return 0;
}

/* Write the superblock back to the start of the image. */
static inline int qfs_write_superblock(FILE *fp, const qfs_superblock *sb)
{
    if (fseek(fp, 0, SEEK_SET) != 0 ||
        fwrite(sb, sizeof(*sb), 1, fp) != 1) {
        fprintf(stderr, "Error: can't write the superblock\n");
        return -1;
    }
    return 0;
}

/* Read directory entry i into *e. */
static inline int qfs_read_entry(FILE *fp, int i, qfs_direntry *e)
{
    if (fseek(fp, qfs_entry_offset(i), SEEK_SET) != 0 ||
        fread(e, sizeof(*e), 1, fp) != 1) {
        fprintf(stderr, "Error: can't read directory entry %d\n", i);
        return -1;
    }
    return 0;
}

/* Write *e to directory entry i. */
static inline int qfs_write_entry(FILE *fp, int i, const qfs_direntry *e)
{
    if (fseek(fp, qfs_entry_offset(i), SEEK_SET) != 0 ||
        fwrite(e, sizeof(*e), 1, fp) != 1) {
        fprintf(stderr, "Error: can't write directory entry %d\n", i);
        return -1;
    }
    return 0;
}

/*
** Search the directory for a file called `name` (case-sensitive). On
** success, copies its entry into *e and returns its index; returns -1 if
** there is no such file. Free entries have an empty name, so they can't
** match a real name. All 255 entries are checked, because a deleted
** file leaves a free entry in the middle of the table.
*/
static inline int qfs_find_file(FILE *fp, const char *name, qfs_direntry *e)
{
    for (int i = 0; i < QFS_NUM_ENTRIES; i++) {
        if (qfs_read_entry(fp, i, e) != 0)
            return -1;
        /*
        ** filename is 23 bytes and should end in a NUL, but strncmp with
        ** the field size means a damaged entry with no NUL can't make us
        ** read past the end of the field.
        */
        if (e->filename[0] != '\0' &&
            strncmp(e->filename, name, sizeof(e->filename)) == 0)
            return i;
    }
    return -1;
}

/*
** Read all B bytes of block b into buf (which must hold at least B
** bytes). Fails if b isn't a real block, which also catches a damaged
** next-block pointer.
*/
static inline int qfs_read_block(FILE *fp, const qfs_superblock *sb,
                                 uint16_t b, uint8_t *buf)
{
    if (b >= sb->total_blocks) {
        fprintf(stderr, "Error: block %u is past the last block (%u)\n",
                b, sb->total_blocks - 1);
        return -1;
    }
    if (fseek(fp, qfs_block_offset(sb, b), SEEK_SET) != 0 ||
        fread(buf, 1, sb->bytes_per_block, fp) != sb->bytes_per_block) {
        fprintf(stderr, "Error: can't read block %u\n", b);
        return -1;
    }
    return 0;
}

/* Write all B bytes of buf to block b. */
static inline int qfs_write_block(FILE *fp, const qfs_superblock *sb,
                                  uint16_t b, const uint8_t *buf)
{
    if (b >= sb->total_blocks) {
        fprintf(stderr, "Error: block %u is past the last block\n", b);
        return -1;
    }
    if (fseek(fp, qfs_block_offset(sb, b), SEEK_SET) != 0 ||
        fwrite(buf, 1, sb->bytes_per_block, fp) != sb->bytes_per_block) {
        fprintf(stderr, "Error: can't write block %u\n", b);
        return -1;
    }
    return 0;
}

/* Write a single flag byte (QFS_BLOCK_BUSY or QFS_BLOCK_FREE) to block b. */
static inline int qfs_set_flag(FILE *fp, const qfs_superblock *sb,
                               uint16_t b, uint8_t flag)
{
    if (fseek(fp, qfs_block_offset(sb, b), SEEK_SET) != 0 ||
        fwrite(&flag, 1, 1, fp) != 1) {
        fprintf(stderr, "Error: can't write the flag of block %u\n", b);
        return -1;
    }
    return 0;
}

#endif /* QFS_H */
