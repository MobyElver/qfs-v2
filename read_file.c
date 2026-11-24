/*
** read_file.c -- Copy a file out of a QFS image
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Usage:   ./read_file <image> <stored_name> <local_name>
**
** Finds <stored_name> in the image's directory and copies its contents to
** a new file called <local_name> in the current directory.
**
** A file is a linked list of blocks. Starting at the entry's
** starting_block, we copy up to B - 3 data bytes from each block, then
** follow the block's next-block field, until file_size bytes have been
** copied. The last block's next-block field should be 0xFFFF.
*/

#include <stdio.h>
#include <stdlib.h>
#include "qfs.h"

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <image> <stored_name> <local_name>\n",
                argv[0]);
        return 1;
    }

    FILE *img = fopen(argv[1], "rb");   /* read-only: we change nothing */
    if (img == NULL) {
        perror(argv[1]);
        return 1;
    }

    qfs_superblock sb;
    if (qfs_read_superblock(img, &sb) != 0) {
        fclose(img);
        return 1;
    }

    /*
    ** Look the file up BEFORE creating the local file, so that a missing
    ** file doesn't leave an empty local file behind.
    */
    qfs_direntry e;
    if (qfs_find_file(img, argv[2], &e) < 0) {
        fprintf(stderr, "Error: %s: no such file in %s\n", argv[2],
                argv[1]);
        fclose(img);
        return 1;
    }

    FILE *out = fopen(argv[3], "wb");
    if (out == NULL) {
        perror(argv[3]);
        fclose(img);
        return 1;
    }

    /*
    ** The block size isn't known until run time, so the block buffer is
    ** allocated on the heap. (A variable-length array would also work.)
    */
    uint16_t B = sb.bytes_per_block;
    uint8_t *buf = malloc(B);
    if (buf == NULL) {
        perror("malloc");
        fclose(out);
        fclose(img);
        return 1;
    }

    uint32_t per_block = qfs_data_per_block(&sb);
    uint32_t remaining = e.file_size;    /* bytes still to copy        */
    uint16_t block = e.starting_block;   /* 0xFFFF for an empty file,  */
    int status = 0;                      /* but then remaining is 0    */

    /*
    ** `steps` counts the blocks visited. A correct file can't use more
    ** blocks than the image has, so a bigger count means the chain loops
    ** back on itself and we stop instead of running forever.
    */
    for (uint32_t steps = 0; remaining > 0; steps++) {
        if (steps >= sb.total_blocks) {
            fprintf(stderr, "Error: block chain of %s loops\n", argv[2]);
            status = 1;
            break;
        }
        if (qfs_read_block(img, &sb, block, buf) != 0) {
            status = 1;
            break;
        }

        /* Every block but the last is full; the last holds the rest. */
        uint32_t n = (remaining < per_block) ? remaining : per_block;
        if (fwrite(buf + 1, 1, n, out) != n) {   /* data starts at byte 1 */
            perror(argv[3]);
            status = 1;
            break;
        }
        remaining -= n;

        uint16_t next = qfs_get_next(buf, B);
        if (remaining == 0 && next != QFS_END_OF_CHAIN)
            fprintf(stderr, "Warning: last block of %s doesn't end the "
                    "chain (next = %u)\n", argv[2], next);
        block = next;
    }

    free(buf);
    if (fclose(out) != 0) {
        perror(argv[3]);
        status = 1;
    }
    fclose(img);
    return status;
}
