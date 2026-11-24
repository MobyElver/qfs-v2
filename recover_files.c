/*
** recover_files.c -- Recover JPEG files from a reformatted QFS image
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Usage:   ./recover_files <image>
**
** When mkfs_qfs formats an image it clears the directory and the
** superblock's counts and sets every block's flag to 0, but it leaves
** each block's data and next-block bytes alone. So the files are still
** there: we have lost their names and sizes, but not their contents or
** the links between their blocks.
**
** The recovery procedure (from the assignment):
**   1. Look at each block in order, from block 0 to the last block. A
**      block starts a JPEG if its first three data bytes (bytes 1, 2, and
**      3 of the block) are FF D8 FF.
**   2. From each such block, follow the next-block pointers, copying the
**      B - 3 data bytes of every block, up to and including the block
**      whose next-block field is 0xFFFF.
**   3. Trim the result just after the LAST FF D9 in it.
**   4. Write the files as recovered_file_1.jpg, recovered_file_2.jpg, and
**      so on, numbered in order of starting block.
**
** Why the LAST FF D9? A JPEG often contains a thumbnail, which is a small
** JPEG with its own FF D8 ... FF D9 inside the big one. Stopping at the
** first FF D9 would cut the file off at the end of the thumbnail.
**
** Why FF D8 FF and not just FF D8? Every JPEG starts with FF D8 followed
** by another marker, which starts with FF. Checking three bytes makes it
** less likely that a block in the middle of some file is mistaken for the
** start of a JPEG.
*/

#include <stdio.h>
#include <stdlib.h>
#include "qfs.h"

/*
** A growable byte array, used to collect a recovered file's data before
** writing it out. We don't know a file's size in advance, and we can't
** trim at the last FF D9 until we've seen all of its blocks.
*/
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} bytebuf;

/* Append n bytes to bb, doubling its capacity when it fills up. */
static int bytebuf_append(bytebuf *bb, const uint8_t *src, size_t n)
{
    if (bb->len + n > bb->cap) {
        size_t newcap = (bb->cap == 0) ? 4096 : bb->cap;
        while (newcap < bb->len + n)
            newcap *= 2;
        uint8_t *p = realloc(bb->data, newcap);
        if (p == NULL) {
            perror("realloc");
            return -1;
        }
        bb->data = p;
        bb->cap = newcap;
    }
    memcpy(bb->data + bb->len, src, n);
    bb->len += n;
    return 0;
}

/*
** Follow the chain that starts at block `first`, appending each block's
** data to bb (step 2). Returns 0 if the chain ends properly with 0xFFFF,
** or -1 if it points outside the image or loops.
*/
static int collect_chain(FILE *fp, const qfs_superblock *sb, uint16_t first,
                         uint8_t *buf, bytebuf *bb)
{
    uint16_t B = sb->bytes_per_block;
    uint16_t block = first;

    for (uint32_t steps = 0; steps < sb->total_blocks; steps++) {
        if (qfs_read_block(fp, sb, block, buf) != 0)
            return -1;
        if (bytebuf_append(bb, buf + 1, qfs_data_per_block(sb)) != 0)
            return -1;

        uint16_t next = qfs_get_next(buf, B);
        if (next == QFS_END_OF_CHAIN)
            return 0;               /* this was the file's last block */
        block = next;
    }
    fprintf(stderr, "Error: the chain starting at block %u loops\n", first);
    return -1;
}

/*
** Return the length of the data up to and including its last FF D9
** (step 3), or 0 if there is no FF D9 at all. Searching backward from the
** end finds the last one first.
*/
static size_t jpeg_length(const uint8_t *data, size_t len)
{
    for (size_t i = len; i >= 2; i--) {
        if (data[i - 2] == 0xFF && data[i - 1] == 0xD9)
            return i;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    /* Read-only, so that a bug can't damage the data we're recovering. */
    FILE *fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    qfs_superblock sb;
    if (qfs_read_superblock(fp, &sb) != 0) {
        fclose(fp);
        return 1;
    }

    uint8_t *buf = malloc(sb.bytes_per_block);
    if (buf == NULL) {
        perror("malloc");
        fclose(fp);
        return 1;
    }

    int count = 0;      /* files recovered so far */
    int status = 0;

    /*
    ** Step 1: check EVERY block, 0 to total_blocks - 1. (Not
    ** available_blocks: that is a count of free blocks, not a block
    ** number.)
    */
    for (uint32_t b = 0; b < sb.total_blocks; b++) {
        if (qfs_read_block(fp, &sb, (uint16_t)b, buf) != 0) {
            status = 1;
            break;
        }
        if (!(buf[1] == 0xFF && buf[2] == 0xD8 && buf[3] == 0xFF))
            continue;               /* doesn't start a JPEG */

        /* Steps 2 and 3. Note: collect_chain reuses buf. */
        bytebuf bb = { NULL, 0, 0 };
        if (collect_chain(fp, &sb, (uint16_t)b, buf, &bb) != 0) {
            fprintf(stderr, "Skipping the JPEG that starts at block %u\n",
                    b);
            free(bb.data);
            continue;
        }
        size_t len = jpeg_length(bb.data, bb.len);
        if (len == 0) {
            fprintf(stderr, "Skipping block %u: no FF D9 end marker\n", b);
            free(bb.data);
            continue;
        }

        /* Step 4: files are numbered from 1, in order of starting block. */
        char name[32];
        snprintf(name, sizeof(name), "recovered_file_%d.jpg", ++count);
        FILE *out = fopen(name, "wb");
        if (out == NULL) {
            perror(name);
            free(bb.data);
            status = 1;
            break;
        }
        if (fwrite(bb.data, 1, len, out) != len) {
            perror(name);
            status = 1;
        }
        fclose(out);
        free(bb.data);

        printf("%s: starting block %u, %zu bytes\n", name, b, len);
    }

    free(buf);
    fclose(fp);
    return status;
}
