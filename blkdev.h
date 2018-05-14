/*
 * file:        blkdev.h
 * description: Block device structure for CS 5600 HW2
 *
 * Peter Desnoyers, Northeastern Computer Science, 2011
 * $Id: blkdev.h 413 2011-11-09 04:18:56Z pjd $
 */
#ifndef __BLKDEV_H__
#define __BLKDEV_H__

#define BLOCK_SIZE 512   /* 512-byte unit for all blkdev addressing in HW3 */

/* A device 'interface' that all RAID implementations will use. An implementation will assign
 * functions to the fields of 'ops', and assign an implementation-specific object to 'private'.
 */
struct blkdev {
    struct blkdev_ops *ops;
    void *private;
};

struct blkdev_ops {
    /* Returns the total number of blocks in the device */
    int  (*num_blocks)(struct blkdev *dev);

    /* Similar to read() of a file descriptor, but reads from a device.
     *   first_blk: first block number to read
     *   num_blks: number of blocks to read
     *   buf: destination buffer to put read bytes
     */
    int  (*read)(struct blkdev * dev, int first_blk, int num_blks, void *buf);

    /* Similar to write() of a file descriptor, but writes to a device.
     *   first_blk: first block number to write
     *   num_blks: number of blocks to write
     *   buf: source buffer where data will come from
     */
    int  (*write)(struct blkdev * dev, int first_blk, int num_blks, void *buf);

    /* Close a device */
    void (*close)(struct blkdev *dev);
};

/* Constants that are returned by the blkdev_ops functions.
 *   SUCCESS - function executed successfully
 *   E_BADADDR - if one of the blocks being operated on is outside the range of 0-N where N is the
 *     maximum block in the device
 *   E_UNAVAIL - if the device could not be read or written. This error means the device has failed.
 *   E_SIZE - an image is not large enough to be used in the RAID device.
 */
enum {SUCCESS = 0, E_BADADDR = -1, E_UNAVAIL = -2, E_SIZE = -3};

/* Create a 'raw' image from a given file */
extern struct blkdev *image_create(char *path);
/* Cause the image to be in a failed state */
extern void image_fail(struct blkdev *);

/* Create a mirror RAID device out of the given blkdev array */
extern struct blkdev *mirror_create(struct blkdev *[2]);
/* Replace a device in a mirror */
extern int mirror_replace(struct blkdev *, int, struct blkdev *);

/* Create a raid0 device */
extern struct blkdev *raid0_create(int, struct blkdev **, int);

/* Create a raid4 device */
extern struct blkdev *raid4_create(int, struct blkdev **, int);

/* Replace a disk in a raid4 device */
extern int raid4_replace(struct blkdev *, int, struct blkdev *);
    
/* The following operations should be used to operate on any blkdev device, whether
 * it be a raw image or one of the RAID devices (mirror, raid0, raid4).
 */

/* Write to a blkdev device */
extern int blkdev_read(struct blkdev * dev, int first_blk, int num_blks, void *buf);
/* Read from a blkdev device */
extern int blkdev_write(struct blkdev * dev, int first_blk, int num_blks, void *buf);
/* Number of blocks in a blkdev device */
extern int blkdev_num_blocks(struct blkdev * dev);
/* Close a blkdev device */
extern void blkdev_close(struct blkdev * dev);

#endif
