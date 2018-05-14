#include "blkdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

/* Write some data to an area of memory */
void write_data(char* data, int length){
    for (int i = 0; i < length; i++){
        data[i] = (char) i;
    }
}

void write_data_char(char* data, int length, char c){
    for (int i = 0; i < length; i++){
        data[i] = c;
    }
}

/* Create a new file ready to be used as an image. Every byte of the file will be zero. */
struct blkdev *  create_new_image(char * path, int blocks){
    if (blocks < 1){
        printf("create_new_image: error - blocks must be at least 1: %d\n", blocks);
        return NULL;
    }
    FILE * image = fopen(path, "w");
    /* This is a trick: instead of writing every byte from 0 to N we can instead move the file cursor
     * directly to N-1 and then write 1 byte. The filesystem will fill in the rest of the bytes with
     * zero for us.
     */
    fseek(image, blocks * BLOCK_SIZE - 1, SEEK_SET);
    char c = 0;
    fwrite(&c, 1, 1, image);
    fclose(image);

    return image_create(path);
}

/* Write a buffer to a file for debugging purposes */
void dump(char* buffer, int length, char* path){
    FILE * output = fopen(path, "w");
    fwrite(buffer, 1, length, output);
    fclose(output);
}

int main(){
    struct blkdev* mirror_drives[3];
    /* Create two images for the mirror */
    mirror_drives[0] = create_new_image("mirror1", 4);
    mirror_drives[1] = create_new_image("mirror2", 4);
    /* Create the raid mirror */
    struct blkdev * mirror = mirror_create(mirror_drives);

    assert(blkdev_num_blocks(mirror) == 4);
    /* Write some data to the mirror, then read the data back and check that the
     * two buffers contain the same bytes.
     */
    char write_buffer[BLOCK_SIZE];
    write_data(write_buffer, BLOCK_SIZE);
    if (blkdev_write(mirror, 0, 1, write_buffer) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }



    char read_buffer[BLOCK_SIZE];
    /* Zero out the buffer to make sure blkdev_read() actually does something */
    bzero(read_buffer, BLOCK_SIZE);

    if (blkdev_read(mirror, 0, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

    /* For debugging, you can analyze these files manually */
    dump(write_buffer, BLOCK_SIZE, "write-buffer");
    dump(read_buffer, BLOCK_SIZE, "read-buffer");

    if (memcmp(write_buffer, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror test passed\n");
    }

    /* Your tests here */
    char write_buffer_A[BLOCK_SIZE];
    write_data_char(write_buffer_A, BLOCK_SIZE, 'A');
    char write_buffer_B[2 * BLOCK_SIZE];
    write_data_char(write_buffer_B, 2*BLOCK_SIZE, 'A');
     if (blkdev_write(mirror, 1, 1, write_buffer_A) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }
     if (blkdev_write(mirror, 2, 2, write_buffer_B) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }
    char test_buffer_AB [3*BLOCK_SIZE];
    write_data_char(test_buffer_AB, 3*BLOCK_SIZE, 'A');
    char read_buffer_AB [3*BLOCK_SIZE];
    bzero(read_buffer_AB, 3 * BLOCK_SIZE);
    if (blkdev_read(mirror, 1, 3, read_buffer_AB) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

    //dump(test_buffer_AB, 3*BLOCK_SIZE, "write-buffer-test");
    //dump(read_buffer_AB, BLOCK_SIZE, "read-buffer");

    if (memcmp(test_buffer_AB, read_buffer_AB, 3*BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror test 1 passed\n");
    }

    write_data_char(write_buffer_A, BLOCK_SIZE, 'B');
    if (blkdev_write(mirror, 0, 1, write_buffer_A) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }

    bzero(read_buffer, BLOCK_SIZE);

    if (blkdev_read(mirror, 0, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

      if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror test 2 passed\n");
    }

    image_fail(mirror_drives[0]);
    mirror = mirror_create(mirror_drives);

    bzero(read_buffer, BLOCK_SIZE);
    if (blkdev_read(mirror, 0, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

      if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror read after image_fail passed\n");
    }

    if (blkdev_write(mirror, 1, 1, write_buffer_A) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }

    bzero(read_buffer, BLOCK_SIZE);
    if (blkdev_read(mirror, 1, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

    if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror write after image_fail passed\n");
    }


    mirror_drives[3] = create_new_image("mirror3", 4);
    mirror_replace(mirror, 0, mirror_drives[3]);

    bzero(read_buffer, BLOCK_SIZE);
    if (blkdev_read(mirror, 0, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

      if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror read after replace passed\n");
    }

    if (blkdev_write(mirror, 2, 1, write_buffer_A) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }

    bzero(read_buffer, BLOCK_SIZE);
    if (blkdev_read(mirror, 2, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

    if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror write after replace passed\n");
    }


    image_fail(mirror_drives[1]);
    mirror = mirror_create(mirror_drives);

    bzero(read_buffer, BLOCK_SIZE);
    if (blkdev_read(mirror, 0, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

      if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror read after replace and fail other disk passed\n");
    }

    write_data_char(write_buffer_A, BLOCK_SIZE, 'C');
    if (blkdev_write(mirror, 1, 1, write_buffer_A) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }

    bzero(read_buffer, BLOCK_SIZE);
    if (blkdev_read(mirror, 1, 1, read_buffer) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

    if (memcmp(write_buffer_A, read_buffer, BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    } else {
        printf("Mirror write after replace and fail other disk passed\n");
    }


}
