/*
** mkfs_qfs.c -- Format a disk image as a QFS version 1 file system
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Usage:   ./mkfs_qfs <image> [<label>]
**
** The image file must already exist; mkfs_qfs never creates, truncates,
** or resizes it. To make a new 1 MiB image and format it:
**
**     dd if=/dev/zero of=test.img bs=1M count=1
**     ./mkfs_qfs test.img MYDISK
**
** Formatting does three things (spec, "Operations"):
**   1. writes a new superblock,
**   2. sets all 8,160 bytes of the directory to 0, and
**   3. writes 0 (free) to the FLAG BYTE ONLY of every data block.
**
** Step 3 is the reason recover_files can work: the old data and next-block
** pointers are still in every block after formatting.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qfs.h"

/*
** Choose the block size from the size of the whole image file (spec,
** "Block Size and Block Count"). Returns 0 if the image is too big.
*/
static uint16_t choose_block_size(long image_size)
{
    if (image_size <= 31457280L)        /* up to 30 MiB  */
        return 512;
    if (image_size <= 62914560L)        /* up to 60 MiB  */
        return 1024;
    if (image_size <= QFS_MAX_IMAGE)    /* up to 120 MiB */
        return 2048;
    return 0;
}

/* A label is at most 14 characters and contains no spaces. */
static int label_is_valid(const char *label)
{
    if (strlen(label) > QFS_MAX_LABEL)
        return 0;
    return strchr(label, ' ') == NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <image> [<label>]\n", argv[0]);
        return 1;
    }

    const char *label = (argc == 3) ? argv[2] : "";
    if (!label_is_valid(label)) {
        fprintf(stderr, "Error: a label is at most %d characters and "
                "has no spaces\n", QFS_MAX_LABEL);
        return 1;
    }

    /*
    ** "rb+" opens an existing file for reading and writing without
    ** changing its size. ("wb+" would truncate it to 0 bytes, which would
    ** destroy the image we are supposed to format.)
    */
    FILE *fp = fopen(argv[1], "rb+");
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    /* The image size is the offset of the end of the file. */
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return 1;
    }
    long image_size = ftell(fp);

    uint16_t B = choose_block_size(image_size);
    if (B == 0) {
        fprintf(stderr, "Error: %s is %ld bytes; a QFS image can be at "
                "most %ld bytes (120 MiB)\n", argv[1], image_size,
                QFS_MAX_IMAGE);
        fclose(fp);
        return 1;
    }

    /*
    ** Every whole block after the first 8,192 bytes is a data block; any
    ** bytes left over at the end are unused. An image must hold at least
    ** one block.
    */
    long num_blocks = (image_size - QFS_DATA_OFFSET) / B;
    if (image_size < QFS_DATA_OFFSET || num_blocks < 1) {
        fprintf(stderr, "Error: %s is %ld bytes; a QFS image needs at "
                "least %d bytes\n", argv[1], image_size,
                QFS_DATA_OFFSET + B);
        fclose(fp);
        return 1;
    }

    /*
    ** Build the superblock. memset first so that the reserved bytes and
    ** the unused bytes of the label are all 0, as the spec requires.
    */
    qfs_superblock sb;
    memset(&sb, 0, sizeof(sb));
    sb.fs_type              = QFS_MAGIC;
    sb.total_blocks         = (uint16_t)num_blocks;
    sb.available_blocks     = (uint16_t)num_blocks;   /* all free */
    sb.bytes_per_block      = B;
    sb.total_direntries     = QFS_NUM_ENTRIES;
    sb.available_direntries = QFS_NUM_ENTRIES;        /* all free */
    sb.version              = QFS_VERSION;
    /* label_is_valid() guaranteed it fits, leaving room for the NUL. */
    strcpy(sb.label, label);

    if (qfs_write_superblock(fp, &sb) != 0) {
        fclose(fp);
        return 1;
    }

    /* Empty the directory: 255 entries of 32 zero bytes. */
    qfs_direntry empty;
    memset(&empty, 0, sizeof(empty));
    for (int i = 0; i < QFS_NUM_ENTRIES; i++) {
        if (qfs_write_entry(fp, i, &empty) != 0) {
            fclose(fp);
            return 1;
        }
    }

    /*
    ** Mark every data block free by writing 0 to its first byte. Nothing
    ** else in the block is touched.
    */
    for (long b = 0; b < num_blocks; b++) {
        if (qfs_set_flag(fp, &sb, (uint16_t)b, QFS_BLOCK_FREE) != 0) {
            fclose(fp);
            return 1;
        }
    }

    /* fclose flushes anything still buffered; check it for errors too. */
    if (fclose(fp) != 0) {
        perror("fclose");
        return 1;
    }
    return 0;
}
