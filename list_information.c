/*
** list_information.c -- Print a QFS image's superblock and directory
**
** CSC310 Operating Systems -- QFS Version 1 reference solution
**
** Usage:   ./list_information <image>
**
** The output format is fixed by the assignment, so that it can be compared
** line by line with the expected output:
**
**     Label:                  SAMPLE
**     Block size:             512 bytes
**     Total blocks:           2032
**     Free blocks:            2027
**     Directory entries:      255
**     Free directory entries: 253
**
**     Name                         Size  Start
**     test.png                     1256      0
**     notes.txt                     510      3
**
** The superblock values are printed as stored, not recomputed, and files
** are listed in directory-table order. An empty file has no starting
** block, so its Start column shows "-".
*/

#include <stdio.h>
#include <string.h>
#include "qfs.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image>\n", argv[0]);
        return 1;
    }

    /* "rb": this program only reads, so open the image read-only. */
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

    /*
    ** Copy the label into a buffer one byte longer than the field and end
    ** it with a NUL ourselves. A valid image always has a NUL in the
    ** label, but this way a damaged one can't make printf run off the end.
    */
    char label[sizeof(sb.label) + 1];
    memcpy(label, sb.label, sizeof(sb.label));
    label[sizeof(sb.label)] = '\0';

    /* Each label is padded to 24 columns, the width of the longest one. */
    printf("%-24s%s\n",       "Label:", label);
    printf("%-24s%u bytes\n", "Block size:", sb.bytes_per_block);
    printf("%-24s%u\n",       "Total blocks:", sb.total_blocks);
    printf("%-24s%u\n",       "Free blocks:", sb.available_blocks);
    printf("%-24s%u\n",       "Directory entries:", sb.total_direntries);
    printf("%-24s%u\n",       "Free directory entries:",
           sb.available_direntries);

    printf("\n%-22s %10s %6s\n", "Name", "Size", "Start");

    for (int i = 0; i < QFS_NUM_ENTRIES; i++) {
        qfs_direntry e;
        if (qfs_read_entry(fp, i, &e) != 0) {
            fclose(fp);
            return 1;
        }
        if (e.filename[0] == '\0')      /* free entry: skip it */
            continue;

        char name[sizeof(e.filename) + 1];  /* same safety as the label */
        memcpy(name, e.filename, sizeof(e.filename));
        name[sizeof(e.filename)] = '\0';

        /* The Start column is a string so that it can also be "-". */
        char start[8];
        if (e.starting_block == QFS_END_OF_CHAIN)
            strcpy(start, "-");
        else
            snprintf(start, sizeof(start), "%u", e.starting_block);

        printf("%-22s %10u %6s\n", name, e.file_size, start);
    }

    fclose(fp);
    return 0;
}
