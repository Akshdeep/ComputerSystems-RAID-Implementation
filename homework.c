/*
 * file:        homework.c
 * description: CS 5600 Homework 3
 *
 * Akshdeep Rungta, Hongxiang Wang
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "blkdev.h"
#include <string.h> 
#include <unistd.h>

/********** MIRRORING ***************/

/* Mirror device
 */
struct mirror_dev {
    struct blkdev *disks[2];
    int nblks;
};
    
static int mirror_num_blocks(struct blkdev *dev) {
    struct mirror_dev * mirror = (struct mirror_dev*) dev->private;
    return mirror->nblks;
}

/* read from one of the sides of the mirror. (if one side has failed,
 * it had better be the other one...) If both sides have failed,
 * return an error.
 * Note that a read operation may return an error to indicate that the
 * underlying device has failed, in which case you should close the
 * device and flag it (e.g. as a null pointer) so you won't try to use
 * it again. 
 */
static int mirror_read(struct blkdev * dev, int first_blk,
                       int num_blks, void *buf)
{
    int val;
    struct mirror_dev * mirror = (struct mirror_dev*) dev->private;
    if (mirror->disks[0] != NULL) {
        val = blkdev_read(mirror->disks[0], first_blk, num_blks, buf);
        if (val == E_UNAVAIL) {
            blkdev_close(mirror->disks[0]);
            mirror->disks[0] = NULL;
        }
        else {
            return val;
        }
    }
    if (mirror->disks[1] != NULL)  {
        val = blkdev_read(mirror->disks[1], first_blk, num_blks, buf);
        if (val == E_UNAVAIL) {
            blkdev_close(mirror->disks[1]);
            mirror->disks[1] = NULL;
        }
        return val;
    }
}

/* write to both sides of the mirror, or the remaining side if one has
 * failed. If both sides have failed, return an error.
 * Note that a write operation may indicate that the underlying device
 * has failed, in which case you should close the device and flag it
 * (e.g. as a null pointer) so you won't try to use it again.
 */
static int mirror_write(struct blkdev * dev, int first_blk,
                        int num_blks, void *buf)
{
    int val1, val2;
    struct mirror_dev * mirror = (struct mirror_dev*) dev->private;
    if (mirror->disks[0] != NULL) {
        val1 = blkdev_write(mirror->disks[0], first_blk, num_blks, buf);
        if (val1 == E_UNAVAIL) {
            blkdev_close(mirror->disks[0]);
            mirror->disks[0] = NULL;
        }
    }
    if (mirror->disks[1] != NULL) {
        val2 = blkdev_write(mirror->disks[1], first_blk, num_blks, buf);
        if (val2 == E_UNAVAIL) {
            blkdev_close(mirror->disks[1]);
            mirror->disks[1] = NULL;
        }        
    }

    if (val1 == SUCCESS || val2 == SUCCESS) {
        return SUCCESS;
    } 
    else {
        return E_UNAVAIL;
    }   
}

/* clean up, including: close any open (i.e. non-failed) devices, and
 * free any data structures you allocated in mirror_create.
 */
static void mirror_close(struct blkdev *dev)
{
    struct mirror_dev * mirror = (struct mirror_dev*) dev->private;
    blkdev_close(mirror->disks[0]);
    blkdev_close(mirror->disks[1]);
    free(mirror);
    dev->private = NULL;
    free(dev);
    return;
}

struct blkdev_ops mirror_ops = {
    .num_blocks = mirror_num_blocks,
    .read = mirror_read,
    .write = mirror_write,
    .close = mirror_close
};

/* create a mirrored volume from two disks. Do not write to the disks
 * in this function - you should assume that they contain identical
 * contents. 
 */
struct blkdev *mirror_create(struct blkdev *disks[2])
{
    struct blkdev *dev = malloc(sizeof(*dev));
    struct mirror_dev *mdev = malloc(sizeof(*mdev));

    if (blkdev_num_blocks(disks[0]) == blkdev_num_blocks(disks[1])) {
        mdev->disks[0] = disks[0];
        mdev->disks[1] = disks[1];
        mdev->nblks = blkdev_num_blocks(disks[0]);
    } 
    else {
        printf("Error: disks size not same.\n");
        return NULL;
    }

    dev->private = mdev;
    dev->ops = &mirror_ops;

    return dev;
}

/* replace failed device 'i' (0 or 1) in a mirror. Note that we assume
 * the upper layer knows which device failed.
 */
int mirror_replace(struct blkdev *volume, int i, struct blkdev *newdisk)
{
    struct mirror_dev * mirror = (struct mirror_dev*) volume->private;
    if (blkdev_num_blocks(newdisk) != mirror->nblks) {
        return E_SIZE;
    }
    char buf[(mirror->nblks) * BLOCK_SIZE];
    blkdev_read(mirror->disks[1-i], 0, mirror->nblks, buf);
    blkdev_write(newdisk, 0, mirror->nblks, buf);
    mirror->disks[i] = newdisk;
    return SUCCESS;
}

/**********  RAID0 ***************/
struct raid0_dev {    
    int unit;
    int N;    
    int state;
    struct blkdev **disks;
};

int raid0_num_blocks(struct blkdev *dev)
{
    struct raid0_dev * raid0 = (struct raid0_dev*) dev->private;    
    return (int) (blkdev_num_blocks(raid0->disks[0])/raid0->unit) * raid0->unit * raid0->N;
}

int get_disk_lba(int blk, int unit, int N) {
    int offset = blk % unit;
    int strip_num = blk/unit;
    int stripe_num = strip_num / N;
    int disk_num = strip_num % N;
    int disk_lba = stripe_num*unit + offset;
    return disk_lba;
}

int get_disk_num(int blk, int unit, int N) {    
    int strip_num = blk/unit;
    int disk_num = strip_num % N;
    return disk_num;
}

/* read blocks from a striped volume. 
 * Note that a read operation may return an error to indicate that the
 * underlying device has failed, in which case you should (a) close the
 * device and (b) return an error on this and all subsequent read or
 * write operations. 
 */
static int raid0_read(struct blkdev * dev, int first_blk,
                       int num_blks, void *buf)
{
    struct raid0_dev * raid0 = (struct raid0_dev*) dev->private; 

    if (raid0->state == 0) {
        return E_UNAVAIL;
    } 

    int disk_num, disk_lba,num_blocks_read,place;
    int blocks = num_blks;
    int LBA = first_blk;
    int val;
    while (blocks > 0){
        disk_num = get_disk_num(LBA, raid0->unit, raid0->N);
        disk_lba = get_disk_lba(LBA, raid0->unit, raid0->N);
        place = disk_lba % raid0->unit; 
        if ((blocks+place) > raid0->unit){
            num_blocks_read = raid0->unit - place;
        }
        else{
            num_blocks_read = blocks;
        } 
        val = blkdev_read(raid0->disks[disk_num], disk_lba, num_blocks_read, buf);
        if(val == E_UNAVAIL) {
            raid0->state = 0;
            blkdev_close(raid0->disks[disk_num]);
            raid0->disks[disk_num] = NULL;
            return E_UNAVAIL;
        }
        buf += num_blocks_read * BLOCK_SIZE;
        blocks -= num_blocks_read;
        LBA+= num_blocks_read;
    }

    return SUCCESS;
}


/* write blocks to a striped volume.
 * Again if an underlying device fails you should close it and return
 * an error for this and all subsequent read or write operations.
 */
static int raid0_write(struct blkdev * dev, int first_blk,
                        int num_blks, void *buf)
{
    struct raid0_dev * raid0 = (struct raid0_dev*) dev->private;

    if (raid0->state == 0) {
        return E_UNAVAIL;
    } 
    int disk_num, disk_lba,num_blocks_read,place;
    int blocks = num_blks;
    int LBA = first_blk;
    int val;
    while (blocks > 0){
        disk_num = get_disk_num(LBA, raid0->unit, raid0->N);
        disk_lba = get_disk_lba(LBA, raid0->unit, raid0->N);
        place = disk_lba % raid0->unit; 
        if ((blocks+place) > raid0->unit){
            num_blocks_read = raid0->unit - place;
        }
        else{
            num_blocks_read = blocks;
        } 
        val = blkdev_write(raid0->disks[disk_num], disk_lba, num_blocks_read, buf);
        if(val == E_UNAVAIL) {
            raid0->state = 0;
            blkdev_close(raid0->disks[disk_num]);
            raid0->disks[disk_num] = NULL;
            return E_UNAVAIL;
        }
        buf += num_blocks_read * BLOCK_SIZE;
        blocks -= num_blocks_read;
        LBA+= num_blocks_read;
        
    }

    return SUCCESS;
}

/* clean up, including: close all devices and free any data structures
 * you allocated in stripe_create. 
 */
static void raid0_close(struct blkdev *dev)
{
    struct raid0_dev * raid0 = (struct raid0_dev*) dev->private;
    for (int i = 0; i< raid0->N; i++) {
        blkdev_close(raid0->disks[i]);
    }
    free(raid0);
    dev->private = NULL;
    free(dev);
    return;
}

struct blkdev_ops raid0_ops = {
    .num_blocks = raid0_num_blocks,
    .read = raid0_read,
    .write = raid0_write,
    .close = raid0_close
};

/* create a striped volume across N disks, with a stripe size of
 * 'unit'. (i.e. if 'unit' is 4, then blocks 0..3 will be on disks[0],
 * 4..7 on disks[1], etc.)
 * Check the size of the disks to compute the final volume size, and
 * fail (return NULL) if they aren't all the same.
 * Do not write to the disks in this function.
 */
struct blkdev *raid0_create(int N, struct blkdev *disks[], int unit)
{
    struct blkdev *dev = malloc(sizeof(*dev));
    struct raid0_dev *sdev = malloc(sizeof(*sdev));

    for (int i = 1; i<N; i++) {
        if (blkdev_num_blocks(disks[0]) != blkdev_num_blocks(disks[i])) {
            printf("Error: disks size not same.\n");
            return NULL;
        }
    }

    sdev->disks = disks;
    sdev->unit = unit;
    sdev->N = N;
    sdev->state = 1;
    dev->private = sdev;
    dev->ops = &raid0_ops;
    return dev;
}

/**********   RAID 4  ***************/

struct raid4_dev {    
    int unit;
    int N;
    int state;
    int disk_failed;
    int nblks;
    struct blkdev **disks;    /* flag bad disk by setting to NULL */    
    struct blkdev *parity;
};

int raid4_num_blocks(struct blkdev *dev)
{
    struct raid4_dev * raid4 = (struct raid4_dev*) dev->private;    
    return raid4->nblks * raid4->N;
}

/* helper function - compute parity function across two blocks of
 * 'len' bytes and put it in a third block. Note that 'dst' can be the
 * same as either 'src1' or 'src2', so to compute parity across N
 * blocks you can do: 
 *
 *     void **block[i] - array of pointers to blocks
 *     dst = <zeros[len]>
 *     for (i = 0; i < N; i++)
 *        parity(block[i], dst, dst);
 *
 * Yes, it could be faster. Don't worry about it.
 */
void parity(int len, void *src1, void *src2, void *dst)
{
    unsigned char *s1 = src1, *s2 = src2, *d = dst;
    int i;
    for (i = 0; i < len; i++)
        d[i] = s1[i] ^ s2[i];
}

int reconstruct_data(struct blkdev *dev, int disk_num, void *buf, int num_blocks_read, int LBA)
{
    struct raid4_dev * raid4 = (struct raid4_dev*) dev->private; 
    char read_buf[BLOCK_SIZE]; 
    int val;
    for(int i = 0; i<num_blocks_read; i++)
    {
        for (int j = 0; j< raid4->N +1; j++)
        {
            if (j != disk_num){
                val = blkdev_read(raid4->disks[j], LBA, 1, read_buf);
                if (val == E_UNAVAIL)
                    return E_UNAVAIL;
                else{
                    parity(BLOCK_SIZE, read_buf, buf, buf);
                }
            }
        }
        LBA++;
        buf+=BLOCK_SIZE;
    }
    return SUCCESS;
}

/* read blocks from a RAID 4 volume.
 * If the volume is in a degraded state you may need to reconstruct
 * data from the other stripes of the stripe set plus parity.
 * If a drive fails during a read and all other drives are
 * operational, close that drive and continue in degraded state.
 * If a drive fails and the volume is already in a degraded state,
 * close the drive and return an error.
 */
static int raid4_read(struct blkdev * dev, int first_blk,
                      int num_blks, void *buf) 
{
    struct raid4_dev * raid4 = (struct raid4_dev*) dev->private; 
    if (raid4->state == -1) {
        return E_UNAVAIL;
    } 
    int val;
    int disk_num,disk_lba,place;
    int j = num_blks;
    int LBA = first_blk;
    while(j > 0){
        int num_blocks_read;
        disk_num = get_disk_num(LBA, raid4->unit, raid4->N);
        disk_lba = get_disk_lba(LBA, raid4->unit, raid4->N);
        place = disk_lba % raid4->unit; 
        if ((j+place) > raid4->unit){
            num_blocks_read = raid4->unit - place;
        }
        else{
            num_blocks_read = j;
        }         
        
        if (raid4->state == 0 && raid4->disk_failed == disk_num){
            memset(buf, '\0', num_blocks_read*BLOCK_SIZE);
            if (reconstruct_data(dev, disk_num, buf, num_blocks_read, disk_lba) == SUCCESS) {
                j -= num_blocks_read;
                LBA += num_blocks_read;
                buf+= num_blocks_read * BLOCK_SIZE;
                continue;
            }  
            else {
                raid4->state = -1;
                return E_UNAVAIL;
            }               
        }
                    
        val = blkdev_read(raid4->disks[disk_num], disk_lba, num_blocks_read, buf);

        if (val == E_UNAVAIL){
            blkdev_close(raid4->disks[disk_num]);
            if (raid4->state ==1){
                raid4->state =0;
                raid4->disk_failed =disk_num;
                continue;
            }
            raid4->state = -1;
            return E_UNAVAIL; 

        }
        j -= num_blocks_read;
        LBA += num_blocks_read;
        buf+= num_blocks_read * BLOCK_SIZE;
    }
    return SUCCESS;
}

void modify(int k, int index, char * read_buf, char* temp)
{
    for (int i = 0; i < BLOCK_SIZE; i++)
        read_buf[k*BLOCK_SIZE+i] = temp[index*BLOCK_SIZE+i];
    return;
}


/* write blocks to a RAID 4 volume.
 * Note that you must handle short writes - i.e. less than a full
 * stripe set. You may either use the optimized algorithm (for N>3
 * read old data, parity, write new data, new parity) or you can read
 * the entire stripe set, modify it, and re-write it. Your code will
 * be graded on correctness, not speed.
 * If an underlying device fails you should close it and complete the
 * write in the degraded state. If a drive fails in the degraded
 * state, close it and return an error.
 * In the degraded state perform all writes to non-failed drives, and
 * forget about the failed one. (parity will handle it)
 */

static int raid4_write(struct blkdev * dev, int first_blk,
                       int num_blks, void *buf)
{
    struct raid4_dev * raid4 = (struct raid4_dev*) dev->private; 
    if (raid4->state == -1) {
        return E_UNAVAIL;
    } 
    int val,val2, val3;
    int disk_num,disk_lba, start,end;
    int LBA = first_blk;
    int j = num_blks;
    int row_count = raid4->unit* raid4->N;
    int index = 0;
    char *read_buf = malloc((row_count+1)*BLOCK_SIZE);
    char *free_buf = read_buf;
    char *temp_buf;
    while (j > 0){        
        memset(free_buf, '\0', (row_count+1)*BLOCK_SIZE);
        read_buf = free_buf;
        temp_buf = read_buf;
        char *temp = buf;
        char parity_buf [raid4->unit*BLOCK_SIZE];
        memset(parity_buf, '\0', raid4->unit*BLOCK_SIZE);
        start = LBA % row_count;
        if (start + j > row_count)
            end = (LBA / row_count + 1) *row_count -1;
        else 
            end = start + j - 1;
        
        disk_lba = get_disk_lba(LBA - start, raid4->unit, raid4->N);
        val = raid4_read(dev, LBA - start, row_count, read_buf);

        if (val == E_UNAVAIL)
            return E_UNAVAIL;

        for(int k = start; k<=end; k++)
        {
            modify(k,index,read_buf,temp);
            index++;
        }   
        
        for (int i =0; i< raid4->N; i++){
            parity(raid4->unit*BLOCK_SIZE,temp_buf,parity_buf,parity_buf);
            temp_buf += raid4->unit*BLOCK_SIZE;
        }
        for (int i = 0; i < raid4->N; ++i){
            if(i != raid4->disk_failed)
                val2 = blkdev_write(raid4->disks[i], disk_lba, raid4->unit,read_buf);
            if (val2 == E_UNAVAIL && i!= raid4->disk_failed)
            {                
                blkdev_close(raid4->disks[i]);
                if (raid4->state == 0) {
                    free(free_buf);
                    raid4->state = -1;
                    return E_UNAVAIL;                    
                }
                raid4->state = 0;
            }
            read_buf += raid4->unit* BLOCK_SIZE;
        }
        val3 = blkdev_write(raid4->parity, disk_lba , raid4->unit, parity_buf);
        if (val3 == E_UNAVAIL &&  raid4->disk_failed!= raid4->N+1)
            {
                blkdev_close(raid4->parity);
                if (raid4->state == 0){
                    free(free_buf);
                    raid4->state = -1;
                    return E_UNAVAIL;                    
                }
                raid4->state = 0;
            }
    
        j -= (end - start + 1);
        LBA += (end - start + 1);
    }
    free(free_buf);
    return SUCCESS;
}

/* clean up, including: close all devices and free any data structures
 * you allocated in raid4_create. 
 */
static void raid4_close(struct blkdev *dev)
{
    struct raid4_dev * raid4 = (struct raid4_dev*) dev->private;
    for (int i = 0; i< raid4->N; i++) {
        blkdev_close(raid4->disks[i]);
    }
    blkdev_close(raid4->parity);
    free(raid4);
    dev->private = NULL;
    free(dev);
    return;
}

struct blkdev_ops raid4_ops = {
    .num_blocks = raid4_num_blocks,
    .read = raid4_read,
    .write = raid4_write,
    .close = raid4_close
};

/* Initialize a RAID 4 volume with strip size 'unit', using
 * disks[N-1] as the parity drive. Do not write to the disks - assume
 * that they are properly initialized with correct parity. (warning -
 * some of the grading scripts may fail if you modify data on the
 * drives in this function)
 */
struct blkdev *raid4_create(int N, struct blkdev *disks[], int unit)
{
    struct blkdev *dev = malloc(sizeof(*dev));
    struct raid4_dev *sdev = malloc(sizeof(*sdev));

    for (int i = 1; i<N; i++) {
        if (blkdev_num_blocks(disks[0]) != blkdev_num_blocks(disks[i])) {
            printf("Error: disks size not same.\n");
            return NULL;
        }
    }
      
    sdev->disks = disks;
    sdev->parity = disks[N-1];
    sdev->state = 1;
    sdev->disk_failed = -1;
    sdev->unit = unit;
    sdev->N = N-1;
    sdev->nblks = (blkdev_num_blocks(disks[0]) / unit) * unit;
    dev->private = sdev;
    dev->ops = &raid4_ops;
    return dev;
}

/* replace failed device 'i' in a RAID 4. Note that we assume
 * the upper layer knows which device failed. You will need to
 * reconstruct content from data and parity before returning
 * from this call.
 */
int raid4_replace(struct blkdev *volume, int i, struct blkdev *newdisk)
{    
    struct raid4_dev * raid4 = (struct raid4_dev*) volume->private;
    if (blkdev_num_blocks(newdisk) < raid4->nblks){
        return E_SIZE;
    }
    char *buf = malloc(blkdev_num_blocks(newdisk) * BLOCK_SIZE);
    char *free_buf = buf;
    int row = blkdev_num_blocks(newdisk)/raid4->unit;
    int LBA = i * raid4->unit;
    for (int j=0; j<row; j++){
        raid4_read(volume, LBA+j*raid4->unit*raid4->N, raid4->unit, buf);
        buf+= raid4->unit * BLOCK_SIZE;
    }
    blkdev_write(newdisk, 0, blkdev_num_blocks(newdisk), buf);
    raid4->disks[i] = newdisk;
    if (i == raid4->N +1)
        raid4->parity = newdisk;

    raid4->state = 1;
    raid4->disk_failed = -1;
    free(free_buf);
    return SUCCESS;
}

