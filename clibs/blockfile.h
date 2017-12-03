#ifndef __BLOCK_FILE_H__
#define __BLOCK_FILE_H__
#include <stdint.h>
#ifdef __cplusplus
extern "C"{
#endif

#define MAX(a,b) ((a)>(b)?(a):(b))

#define BLOCKFILE_HEAD_VERSION 1

typedef void* blockfile_t;
typedef struct {
	char magic[4];         //"BFI\0"
    int16_t version;
    int16_t int16_rest;
    int64_t filesize;       //文件大小.
	int32_t block_size;    //分块大小
	int32_t block_cnt;     //块总数
	int32_t block_processing;  //正在处理中的数量 (每次从metafile中加载时，要重新设置为0)
	int32_t block_processed;   //已经处理完成的。(每次从metafile中加载时，要重新修改该值)
	char md5[16];              //文件的md5值。
    uint32_t create_time;       //创建时间。
    char extinfo[972];             //保留位，凑齐1024字节
    char bits[8];               //bits是正式的位索引信息, 不算在文件头内容.
}blockfile_head_t;

blockfile_t x_open(const char* filename, const char* tmpfilename);
int x_close(blockfile_t blockfile, blockfile_head_t* head, int chk_md5_sum=0);

int x_set_make_dest_dir(blockfile_t x, int make_dest_dir);

int x_block_is_processed(blockfile_t blockfile, const int block_index);
int x_block_set_processed(blockfile_t blockfile, const int block_idx);

int x_head_is_inited(blockfile_t blockfile);
int x_head_init(blockfile_t blockfile, const int64_t filesize, const int block_size, 
                uint32_t create_time, char* extinfo);

int x_block_write(blockfile_t blockfile, const int block_idx, const int writed, const char* buf, const int size);
int x_block_read(blockfile_t blockfile, const int block_idx, char* buf, const int size);

int x_processed_ok(blockfile_t blockfile);

blockfile_head_t* x_get_file_head(blockfile_t blockfile);
int x_read_head(const char* metafilename, blockfile_head_t* head);

const char* x_so_version();

#ifdef __cplusplus
}
#endif

#endif