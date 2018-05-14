#include "blkdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

void write1(struct blkdev* dev,int addr,int len,int seq,int *array){
	char buf[len*BLOCK_SIZE];

	for (int i = 0; i< len; i++) {
		sprintf(&buf[i*BLOCK_SIZE], "%d", seq);
		array[addr + i] = seq;
	}

	if (blkdev_write(dev, addr, len, buf) != SUCCESS){
        printf("Write failed!\n");
        exit(0);
    }
    
}

void verify(struct blkdev* dev,int addr,int len,int *array) {
	char buf[len*BLOCK_SIZE];
	if (blkdev_read(dev, addr, len, buf) != SUCCESS){
        printf("Read failed!\n");
        exit(0);
    }

    for (int i = 0; i < len; i++)
    {

    	if (array[addr + i] != 0) {
    		assert(atoi(&buf[i*BLOCK_SIZE]) == array[addr + i]);
    	}    	
    }
}

void random_write1(struct blkdev* dev,int seq,int *array, int max) {
	int addr = rand() % max;
	int len = rand() % (max - addr) + 1;
	write1(dev, addr, len, seq, array);
}

void random_verify1(struct blkdev* dev, int *array, int max) {
	int addr = rand() % max;
	int len = rand() % (max - addr) + 1;
	verify(dev, addr, len, array);
}

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

int main(){
	int strip_size[4] ={2,4,7,32};
	int num_disk[4] = {3, 4, 5, 6};
	int num_blocks[4] = {12, 32, 70, 384};
	
	for (int i = 0; i < 4; i++)
	{
		struct blkdev* raid0_drives[num_disk[i]];
		for (int j = 0; j < num_disk[i]; j++){
			char raid_name[8];
			sprintf(raid_name, "raid0_%d", j);
			raid0_drives[j] = create_new_image(raid_name, 2*strip_size[i]);
		}
		struct blkdev * raid0 = raid0_create(num_disk[i],raid0_drives,strip_size[i]);

		assert(blkdev_num_blocks(raid0) == num_blocks[i]);

		int *array = (int *) malloc((blkdev_num_blocks(raid0)*BLOCK_SIZE)*sizeof(int));
		memset(array, 0, (blkdev_num_blocks(raid0)*BLOCK_SIZE)*sizeof(int));
		int seq = 1;

		int max = 2*strip_size[i] * num_disk[i];
		for (int i = 0; i < 5; i++)
		{
			random_write1(raid0, seq, array, max);
			seq++;
		}    

		for (int i = 0; i < 5; i++)
		{
			random_verify1(raid0, array, max);
		}

	}
	
	struct blkdev* raid0_drives[6];
	for (int j = 0; j < 6; j++){
			char raid_name[8];
			sprintf(raid_name, "raid0_%d", j);
			raid0_drives[j] = create_new_image(raid_name, 2*32);
		}
	struct blkdev * raid0 = raid0_create(6,raid0_drives,32);
	image_fail(raid0_drives[1]);
	char buf[64*BLOCK_SIZE];
	int val = blkdev_read(raid0, 0, 64, buf);
	assert(val == E_UNAVAIL);

	val = blkdev_write(raid0, 0, 64, buf);
	assert(val == E_UNAVAIL);

	printf("raid0 tests passed.\n");
}