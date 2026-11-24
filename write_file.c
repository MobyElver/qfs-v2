/*
** write_file.c -- Add a file to a QFS image (optional program)
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Usage:   ./write_file <image> <local_name>
**
** Copies <local_name> from the current directory into the image, stored
** under the same name. Adding a file (spec, "Operations"):
**   - use the first free directory entry, and take free blocks
**     lowest-numbered first;
**   - in each block write the flag (1), the data, and the next-block field
**     (0xFFFF in the last block);
**   - fill in the entry: permissions 0x17, owner 0, group 0, the size, and
**     the starting block (0xFFFF for an empty file, which uses no blocks);
**   - subtract the blocks used from available_blocks and 1 from
**     available_direntries.
**
** Every check happens before anything is written: if the name is too
** long, already used, or there's no room, the image is left unchanged.
**
** In the last block, only the bytes that hold the file are written. The
** rest of that block's data area isn't part of the file, so it is left as
** it was.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qfs.h"

/*
** Read the whole local file into memory. Returns a malloc'd buffer (which
** the caller frees) and stores the size in *size, or returns NULL on
** error. malloc(0) may return NULL, so we always ask for at least 1 byte.
*/
static uint8_t *read_local_file(const char *name, uint32_t *size)
{
    FILE *in = fopen(name, "rb");
    if (in == NULL) {
        perror(name);
        return NULL;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        perror(name);
        fclose(in);
        return NULL;
    }
    long len = ftell(in);
    rewind(in);
    if (len < 0 || len > QFS_MAX_IMAGE) {    /* can't fit in any image */
        fprintf(stderr, "Error: %s is too large for a QFS image\n", name);
        fclose(in);
        return NULL;
    }

    uint8_t *data = malloc(len > 0 ? (size_t)len : 1);
    if (data == NULL) {
        perror("malloc");
        fclose(in);
        return NULL;
    }
    if (fread(data, 1, (size_t)len, in) != (size_t)len) {
        fprintf(stderr, "Error: can't read %s\n", name);
        free(data);
        fclose(in);
        return NULL;
    }
    fclose(in);
    *size = (uint32_t)len;
    return data;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image> <local_name>\n", argv[0]);
        return 1;
    }
    const char *name = argv[2];

    /* Names are 1-22 characters with no '/' (spec, "Directory Entries"). */
    if (strlen(name) == 0 || strlen(name) > QFS_MAX_NAME ||
        strchr(name, '/') != NULL) {
        fprintf(stderr, "Error: a QFS file name is 1-%d characters with "
                "no '/'\n", QFS_MAX_NAME);
        return 1;
    }

    uint32_t size;
    uint8_t *data = read_local_file(name, &size);
    if (data == NULL)
        return 1;

    FILE *fp = fopen(argv[1], "rb+");
    if (fp == NULL) {
        perror(argv[1]);
        free(data);
        return 1;
    }

    /*
    ** Everything that has to be freed is set up before the first check,
    ** so that every failure can jump to one cleanup point. `status`
    ** starts as "failed" and is set to 0 only once the file is written.
    */
    int status = 1;
    uint16_t *blocks = NULL;
    uint8_t *buf = NULL;
    qfs_superblock sb;
    qfs_direntry e;

    if (qfs_read_superblock(fp, &sb) != 0)
        goto done;

    /* Check 1: no file of that name already exists. */
    if (qfs_find_file(fp, name, &e) >= 0) {
        fprintf(stderr, "Error: %s already exists in %s\n", name, argv[1]);
        goto done;
    }

    /* Check 2: find the first free directory entry. */
    int index = -1;
    for (int i = 0; i < QFS_NUM_ENTRIES && index < 0; i++) {
        if (qfs_read_entry(fp, i, &e) != 0)
            goto done;
        if (e.filename[0] == '\0')
            index = i;
    }
    if (index < 0) {
        fprintf(stderr, "Error: the directory of %s is full\n", argv[1]);
        goto done;
    }

    /*
    ** Check 3: find n free blocks, lowest-numbered first, by reading each
    ** block's flag byte. The flags are the real record of which blocks are
    ** free, so we search them rather than trusting available_blocks.
    */
    uint32_t n = qfs_blocks_needed(&sb, size);
    blocks = malloc((n > 0 ? n : 1) * sizeof(uint16_t));
    buf = malloc(sb.bytes_per_block);
    if (blocks == NULL || buf == NULL) {
        perror("malloc");
        goto done;
    }

    uint32_t found = 0;
    for (uint32_t b = 0; b < sb.total_blocks && found < n; b++) {
        uint8_t flag;
        if (fseek(fp, qfs_block_offset(&sb, (uint16_t)b), SEEK_SET) != 0 ||
            fread(&flag, 1, 1, fp) != 1) {
            fprintf(stderr, "Error: can't read block %u\n", b);
            goto done;
        }
        if (flag == QFS_BLOCK_FREE)
            blocks[found++] = (uint16_t)b;
    }
    if (found < n) {
        fprintf(stderr, "Error: %s needs %u blocks but %s has only %u "
                "free\n", name, n, argv[1], found);
        goto done;
    }

    /*
    ** All checks passed; now write. Each block is read first so that the
    ** bytes we don't change (the unused end of the last block) keep their
    ** old values; then the flag, data, and next-block field are filled in
    ** and the whole block is written back.
    */
    uint32_t per_block = qfs_data_per_block(&sb);
    for (uint32_t i = 0; i < n; i++) {
        if (qfs_read_block(fp, &sb, blocks[i], buf) != 0)
            goto done;

        uint32_t offset = i * per_block;            /* where this chunk */
        uint32_t chunk = size - offset;             /* starts in the    */
        if (chunk > per_block)                      /* file, and how    */
            chunk = per_block;                      /* long it is       */

        buf[0] = QFS_BLOCK_BUSY;
        memcpy(buf + 1, data + offset, chunk);
        qfs_set_next(buf, sb.bytes_per_block,
                     (i + 1 < n) ? blocks[i + 1] : QFS_END_OF_CHAIN);

        if (qfs_write_block(fp, &sb, blocks[i], buf) != 0)
            goto done;
    }

    /* Fill in the directory entry. memset leaves the name NUL-padded. */
    memset(&e, 0, sizeof(e));
    strcpy(e.filename, name);           /* fits: checked length above */
    e.permissions    = QFS_DEFAULT_PERMS;
    e.owner_id       = 0;
    e.group_id       = 0;
    e.starting_block = (n > 0) ? blocks[0] : QFS_END_OF_CHAIN;
    e.file_size      = size;
    if (qfs_write_entry(fp, index, &e) != 0)
        goto done;

    /* Update the free counts in the superblock. */
    sb.available_blocks     -= n;
    sb.available_direntries -= 1;
    if (qfs_write_superblock(fp, &sb) != 0)
        goto done;

    status = 0;     /* success */

done:
    free(blocks);
    free(buf);
    free(data);
    if (fclose(fp) != 0) {
        perror("fclose");
        status = 1;
    }
    return status;
}
