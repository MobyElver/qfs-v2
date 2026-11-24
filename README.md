# QFS Version 1 — Reference Solution

CSC310 Operating Systems

This is a complete solution to the QFS Version 1 final project, including
both optional programs. It follows *The Quinnipiac File System (QFS),
Specification, Version 1*; section names in the comments ("spec,
Operations") refer to that document.

## Files

| File | What it does |
|---|---|
| `qfs.h` | Constants from the spec, the packed on-disk structures, and helper functions shared by every program |
| `mkfs_qfs.c` | Formats an existing image file |
| `list_information.c` | Prints the superblock information and the directory |
| `read_file.c` | Copies a file out of an image |
| `recover_files.c` | Recovers JPEGs from an image that has been reformatted |
| `delete_file.c` | Deletes a file from an image |
| `write_file.c` | Adds a file to an image |
| `Makefile` | Builds every program |

## Building

```
make            # build everything
make clean      # remove the programs
```

The programs compile with `-Wall -Wextra` and no warnings.

## Running

```
./mkfs_qfs <image> [<label>]
./list_information <image>
./read_file <image> <stored_name> <local_name>
./recover_files <image>
./delete_file <image> <stored_name>
./write_file <image> <local_name>
```

Every program exits with status 0 when it succeeds. When it can't do what
it was asked, it prints a one-line message to standard error, leaves the
image unchanged, and exits with status 1.

## A quick tour

```
dd if=/dev/zero of=test.img bs=1M count=1    # a 1 MiB empty file
./mkfs_qfs test.img SAMPLE                   # format it
./write_file test.img notes.txt              # add a file
./list_information test.img                  # see it in the directory
./read_file test.img notes.txt copy.txt      # copy it back out
cmp notes.txt copy.txt                       # no output: identical
xxd -s 8192 -l 512 test.img                  # look at block 0
```

## How the code is organized

**Shared code lives in `qfs.h`.** Each program is built from a single `.c`
file, so the helpers are `static inline` functions in the header rather
than a separate library. `qfs_find_file`, `qfs_read_block`, and
`qfs_get_next` are the building blocks for everything that follows a
chain of blocks.

**Data blocks are read as byte arrays, not structures.** A block's
next-block field is at offset `B - 2`, and `B` isn't known until the
superblock has been read. Each program reads a whole block into a buffer of
`B` bytes and uses `buf[0]` (flag), `buf + 1` (data), and
`qfs_get_next(buf, B)` (next block).

**Programs that only read open the image with `"rb"`.** `list_information`,
`read_file`, and `recover_files` can't change the image even if they have a
bug.

**Programs that change the image check everything first.** `write_file`
checks the name, looks for a free directory entry, and finds every block
it needs before writing a single byte. `delete_file` walks the whole chain
before freeing anything. If a check fails, the image is exactly as it was.

**Chains are never trusted blindly.** Every block number is checked
against `total_blocks` before it's used, and a walk that visits more
blocks than the image has is stopped, so a damaged image produces an error
message instead of a crash or an endless loop.

**Formatting and deleting only touch flag bytes.** Neither one erases file
data, which is exactly why `recover_files` can find files after a
reformat. `write_file` follows the same idea: in a file's last block it
writes only the bytes that belong to the file and leaves the rest of the
block as it was.

## Recovery

`recover_files` looks at every block, from 0 to the last, for one whose
first three data bytes are `FF D8 FF`. From each one it follows the
next-block pointers to the block marked `0xFFFF`, then trims the result
just after the **last** `FF D9`, because a JPEG can contain a thumbnail
with its own `FF D8 … FF D9` inside. Files are named
`recovered_file_1.jpg`, `recovered_file_2.jpg`, … in order of starting
block.
# qfs-v2
