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

void write_data_char(char* data, int length, char c){
    for (int i = 0; i < length; i++){
        data[i] = c;
    }
}

void dump(char* buffer, int length, char* path){
    FILE * output = fopen(path, "w");
    fwrite(buffer, 1, length, output);
    fclose(output);
}

int main(){
	int strip_size[4] ={2,4,7,32};
	int num_disk[4] = {3, 4, 5, 6};
	int num_blocks[4] = {8, 24, 56, 320};
	
	for (int i = 0; i < 4; i++)
	{
		struct blkdev* raid4_drives[num_disk[i]];
		for (int j = 0; j < num_disk[i]; j++){
			char raid_name[8];
			sprintf(raid_name, "raid4_%d", j);
			raid4_drives[j] = create_new_image(raid_name, 2*strip_size[i]);
		}
		struct blkdev * raid4 = raid4_create(num_disk[i],raid4_drives,strip_size[i]);

		assert(blkdev_num_blocks(raid4) == num_blocks[i]);

		int *array = (int *) malloc((blkdev_num_blocks(raid4)*BLOCK_SIZE)*sizeof(int));
		memset(array, 0, (blkdev_num_blocks(raid4)*BLOCK_SIZE)*sizeof(int));
		int seq = 1;

		int max = 2*strip_size[i] * (num_disk[i] - 1);
		for (int i = 0; i < 5; i++)
		{
			random_write1(raid4, seq, array, max);
			seq++;
		}    

		for (int i = 0; i < 5; i++)
		{
			random_verify1(raid4, array, max);
		}

	}

	struct blkdev* raid4_drives[4];
	struct blkdev* raid4_new;
	raid4_new = create_new_image("raid4_new", 16);
	printf("%d\n", blkdev_num_blocks(raid4_new));
	for (int j = 0; j < 4; j++){
			char raid_name[8];
			sprintf(raid_name, "raid4_%d", j);
			raid4_drives[j] = create_new_image(raid_name, 2*8);
		}
	struct blkdev * raid4 = raid4_create(4,raid4_drives,8);
	char buf[24*BLOCK_SIZE];
	char buf_read[24*BLOCK_SIZE];
	write_data_char(buf, 24*BLOCK_SIZE, 'A');
	int val = blkdev_write(raid4, 0, 24, buf);
	image_fail(raid4_drives[1]);
	val = blkdev_read(raid4, 0, 24, buf_read);
	assert(val == SUCCESS);
	if (memcmp(buf, buf_read, 24*BLOCK_SIZE) != 0){
        printf("Read doesn't match write!\n");
    }	

    raid4_replace(raid4,1,raid4_new);
    char buf_rp[2*BLOCK_SIZE];
	char buf_read_rp[2*BLOCK_SIZE];
	write_data_char(buf_rp, 2*BLOCK_SIZE, 'B');
	val = blkdev_write(raid4, 8, 2, buf_rp);
	assert(val == SUCCESS);
   	val = blkdev_read(raid4, 8, 2, buf_read_rp);
	assert(val == SUCCESS);
	if (memcmp(buf_rp, buf_read_rp, 2*BLOCK_SIZE) != 0){
        printf("Read doesn't match write after replace!\n");
    }	

    image_fail(raid4_drives[2]);

   	val = blkdev_read(raid4, 0, 24, buf_read);
   	assert (val == SUCCESS);

   	val = blkdev_write(raid4, 8, 2, buf);
   	assert (val == SUCCESS);

   	image_fail(raid4_drives[0]);
   	 	val = blkdev_read(raid4, 0, 24, buf_read);
   	assert (val == E_UNAVAIL);

   	val = blkdev_write(raid4, 8, 2, buf);
   	assert (val == E_UNAVAIL);

    dump(buf_read, 24*BLOCK_SIZE, "raid4_read_test");
	printf("raid4 tests passed.\n");
}