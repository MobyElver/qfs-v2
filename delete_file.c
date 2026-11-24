/*
** delete_file.c -- Delete a file from a QFS image (optional program)
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Usage:   ./delete_file <image> <stored_name>
**
** Deleting a file (spec, "Operations"):
**   - write 0 (free) to the flag byte of every block in the file's chain,
**     leaving the data and next-block bytes as they are;
**   - set all 32 bytes of the file's directory entry to 0; and
**   - add the number of freed blocks to available_blocks and 1 to
**     available_direntries.
**
** Like a real file system, this doesn't erase the file's data. That is
** what makes undelete and recovery tools possible.
**
** The chain is checked completely before anything is written, so a
** damaged chain leaves the image unchanged.
*/

#include <stdio.h>
#include <stdlib.h>
#include "qfs.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image> <stored_name>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb+");   /* read and write, don't truncate */
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    qfs_superblock sb;
    if (qfs_read_superblock(fp, &sb) != 0) {
        fclose(fp);
        return 1;
    }

    qfs_direntry e;
    int index = qfs_find_file(fp, argv[2], &e);
    if (index < 0) {
        fprintf(stderr, "Error: %s: no such file in %s\n", argv[2],
                argv[1]);
        fclose(fp);
        return 1;
    }

    /*
    ** Pass 1: walk the chain and remember its block numbers. The file
    ** uses exactly ceil(file_size / (B - 3)) blocks; an empty file uses
    ** none, so the list stays empty.
    */
    uint32_t n = qfs_blocks_needed(&sb, e.file_size);
    uint16_t *blocks = malloc((n > 0 ? n : 1) * sizeof(uint16_t));
    uint8_t  *buf    = malloc(sb.bytes_per_block);
    if (blocks == NULL || buf == NULL) {
        perror("malloc");
        free(blocks);
        free(buf);
        fclose(fp);
        return 1;
    }

    uint16_t block = e.starting_block;
    for (uint32_t i = 0; i < n; i++) {
        if (qfs_read_block(fp, &sb, block, buf) != 0) {
            fprintf(stderr, "Error: %s has a damaged block chain; "
                    "nothing was deleted\n", argv[2]);
            free(blocks);
            free(buf);
            fclose(fp);
            return 1;
        }
        blocks[i] = block;
        block = qfs_get_next(buf, sb.bytes_per_block);
    }

    /* Pass 2: free the blocks. Only the flag byte of each is written. */
    for (uint32_t i = 0; i < n; i++) {
        if (qfs_set_flag(fp, &sb, blocks[i], QFS_BLOCK_FREE) != 0) {
            free(blocks);
            free(buf);
            fclose(fp);
            return 1;
        }
    }

    /* Clear the whole directory entry. */
    qfs_direntry empty;
    memset(&empty, 0, sizeof(empty));
    if (qfs_write_entry(fp, index, &empty) != 0) {
        free(blocks);
        free(buf);
        fclose(fp);
        return 1;
    }

    /* Update the free counts in the superblock. */
    sb.available_blocks     += n;
    sb.available_direntries += 1;
    int status = (qfs_write_superblock(fp, &sb) == 0) ? 0 : 1;

    free(blocks);
    free(buf);
    if (fclose(fp) != 0) {
        perror("fclose");
        status = 1;
    }
    return status;
}
