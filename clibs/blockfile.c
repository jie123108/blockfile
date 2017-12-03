/***********************************
 * author: jie123108@163.com
 * date: 20171202
 ***********************************/

#include "pub.h"
#include "md5.h"
#include "Filelock.h"
#include "blockfile.h"
#include "charcodec.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/mman.h>
#include <stddef.h>
 
#define CHAR_SIZE 8
#define BITMASK(b) (1 << ((b) % CHAR_SIZE))
#define BITSLOT(b) ((b) / CHAR_SIZE)
#define BITSET(arr, b) ((arr)[BITSLOT(b)] |= BITMASK(b))
#define BITCLEAR(arr, b) ((arr)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(arr, b) ((arr)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + CHAR_SIZE - 1)/CHAR_SIZE)
#define BITARRAY(arr, size); char arr[BITNSLOTS(size)];

#ifdef __APPLE__
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

#define BLOCKFILE_MAGIC "BFI"
#define BLOCKFILE_HEAD_SIZE sizeof(blockfile_head_t)

//根据文件大小，块大小计算块数。
#define blocks(filesize, block_size) ((filesize+block_size-1)/block_size)

typedef struct {
	char* filename;
	char* datafilename;
	char* metafilename;
	int datafile;
	int metafile;
	int32_t make_dest_dir:1; //是否创建目标目录(filename对应的目录)。
	int64_t datafilesize;
	int64_t metafilesize;
	blockfile_head_t* head; //文件头信息。
}blockfile_data_t;

#define IsExist(szFileName) (access(szFileName, F_OK)==0)


EXPORT int GetFilePath(const char* fullname, char* path, int size)
{
	if(fullname == NULL || path == NULL){
		return -1;
	}

	char* pPathEnd = strrchr((char*)fullname, '/');
	if(pPathEnd == NULL){
		return -1;
	}
	pPathEnd++;

	if(pPathEnd-fullname < size){
		size = pPathEnd-fullname;
	}
	
	strncpy(path, fullname, size);
	
	return 0;
}

EXPORT int ForceMkdir(const char* path)
{
	if(path == NULL){
		return -1;
	}
	if(strlen(path)==0){
		errno = 0;
		return 0;
	}

	if(access(path, F_OK)!=0){
		int len = (int)strlen(path);
		char* cmd = (char*)alloca(len+32);
		memset(cmd,0,len+32);

		sprintf(cmd, "mkdir -p %s", path);
		int ret = system(cmd);
		return ret/256;
	}else{
		//LOG_INFO("path %s aready exists!", path);
	}
	errno = 0;
	return 0;
}

EXPORT int x_read_head(const char* metafilename, blockfile_head_t* head)
{
	if(metafilename == NULL || head == NULL){
		return -1;
	}

	if(!IsExist(metafilename)){
		return -1;
	}
	int metafile = open(metafilename, O_RDONLY);
	if(metafile == -1){
		LOG_ERROR("open file [%s] failed! err:%s", metafilename, strerror(errno));
		return -1;
	}

	int ret = read(metafile, head, sizeof(blockfile_head_t));
	if(ret != sizeof(blockfile_head_t)){
		LOG_ERROR("read metafile head from [%s] failed! ret=%d err:%s", metafilename, ret, strerror(errno));
		ret = -1;
	}else{
		if(strcmp(head->magic, BLOCKFILE_MAGIC)!=0){
			LOG_ERROR("read metafile head from [%s] failed! magic [0x%04x] invalid!", metafilename, head->magic);
			ret = -1;
		}else{
			ret = 0;
		}
	}
	close(metafile);

	errno = 0;
	return 0;
}


EXPORT blockfile_t x_open(const char* filename, const char* tmpfilename)
{ 
	if(filename == NULL){
		return NULL;
	}
	if(tmpfilename == NULL){
		tmpfilename = filename;
	}

	blockfile_data_t* blockfile = (blockfile_data_t*)calloc(1, sizeof(blockfile_data_t));
	if(blockfile == NULL){
		return NULL;
	}

	int filename_len = strlen(filename);
	blockfile->filename = strndup(filename, filename_len);
	if(blockfile->filename == NULL){
		x_close(blockfile, NULL);
		return NULL;
	}

	int tmpfilename_len = strlen(tmpfilename);
	blockfile->datafilename = (char*)calloc(1, tmpfilename_len+8);
	if(blockfile->datafilename == NULL){
		x_close(blockfile, NULL);
		return NULL;
	}
	sprintf(blockfile->datafilename, "%s.dat", tmpfilename);

	blockfile->metafilename = (char*)calloc(1, tmpfilename_len+8);
	if(blockfile->metafilename == NULL){
		x_close(blockfile, NULL);
		return NULL;
	}
	sprintf(blockfile->metafilename, "%s.mt", tmpfilename);

	if(IsExist(blockfile->metafilename) && !IsExist(blockfile->datafilename)){
		int ret = unlink(blockfile->metafilename);
		if(ret != 0){
			LOG_ERROR("unlink(%s) failed! %d err:%s", blockfile->metafilename, errno, strerror(errno));
		}else{
			LOG_INFO("unlink(%s) success!", blockfile->metafilename);
		}
	}

	blockfile->metafile = open(blockfile->metafilename, O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if(blockfile->metafile == -1){
		// LOG_INFO("meta file [%s] failed! err:%s", blockfile->metafilename, strerror(errno));
		// 不存在的情况, 也可以打开成功, 可以在后面head init中再重新打开.
		blockfile->datafile = -1;
		return blockfile;
	}else{
		blockfile->datafile = open(blockfile->datafilename, O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		LOG_DEBUG("open localfile [%s] file=%d", blockfile->datafilename, blockfile->datafile);
		if(blockfile->datafile == -1){
			LOG_ERROR("open file [%s] failed! err:%s", blockfile->datafilename, strerror(errno));
			x_close(blockfile, NULL);
			return NULL;
		}

		int ret = 0;

		int64_t meta_file_size = GetFileSize(blockfile->metafile);
		LOG_INFO("metafile [%s] size:%lld", blockfile->metafilename, meta_file_size);
		blockfile->metafilesize = meta_file_size;

		blockfile_head_t * x_head = (blockfile_head_t*)mmap(NULL, meta_file_size, PROT_READ|PROT_WRITE, MAP_SHARED, blockfile->metafile, 0);
		if(x_head == MAP_FAILED){
			if(errno == ENOMEM){
				LOG_ERROR("mmap failed no memory!");
			}else{
				LOG_ERROR("mmap failed! %d err:%s", errno, strerror(errno));
			}
			x_close(blockfile, NULL);
			return NULL;
		}
		//LOG_INFO("########## xhead maped ##############");
		blockfile->head = x_head;
		//校验x_head
		if(strcmp(x_head->magic, BLOCKFILE_MAGIC)!=0){
			LOG_ERROR("meta file [%s] invalid! magic error", blockfile->metafilename);
			x_close(blockfile, NULL);
			return NULL;
		}

		//重新计算已经处理的块数。
		int x;
		int block_processed = 0;
		for(x=0;x<x_head->block_cnt;x++){
			//已经处理完的。
			if(BITTEST(x_head->bits, x)){
				block_processed++;
			}
		}
		
		x_head->block_processed = block_processed;
	}

	return blockfile;
}

int x_set_make_dest_dir(blockfile_t x, int make_dest_dir)
{
	if(x == NULL){
		return 0;
	}
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	blockfile->make_dest_dir = make_dest_dir>0;
}

int x_processed_ok(blockfile_t x)
{
	if(x == NULL){
		return 0;
	}
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile->filename != NULL && IsExist(blockfile->filename)){
		//已经被重命名。
		return 1;
	}

	if(blockfile->head == NULL){
		return 0;
	}
	int ret = blockfile->head->block_cnt> 0 && blockfile->head->block_cnt == blockfile->head->block_processed;
	LOG_INFO("blockfile->head->block_cnt(%d) blockfile->head->block_processed(%d)", 
		blockfile->head->block_cnt, blockfile->head->block_processed);
	return ret;
}

int x_close(blockfile_t x, blockfile_head_t* head, int chk_md5_sum)
{
	int ret = 0;
	blockfile_data_t* blockfile = (blockfile_data_t*)x;

	if(blockfile == NULL){
		return 0;
	}

	assert(blockfile->filename != NULL);
	assert(blockfile->datafilename != NULL);
	assert(blockfile->metafilename != NULL);

	int locked = 0;
	int all_processed = 0;
		
	if(blockfile->head == NULL){
		if(blockfile->metafile > 0){
			//fsync(blockfile->metafile);
			close(blockfile->metafile);
			blockfile->metafile = 0;
		}
		if(blockfile->datafile > 0){
			close(blockfile->datafile);
			blockfile->datafile = 0;
		}	

		if(IsExist(blockfile->datafilename)){
			ret = unlink(blockfile->datafilename);
			if(ret != 0){
				LOG_ERROR("unlink(%s) failed! %d err:%s", blockfile->datafilename, errno, strerror(errno));
			}else{
				LOG_INFO("unlink(%s) success!", blockfile->datafilename);
			}
		}
		if(IsExist(blockfile->metafilename)){
			ret = unlink(blockfile->metafilename);
			if(ret != 0){
				LOG_ERROR("unlink(%s) failed! %d err:%s", blockfile->metafilename, errno, strerror(errno));
			}else{
				LOG_INFO("unlink(%s) success!", blockfile->metafilename);
			}
		}
	}else{		
		all_processed = blockfile->head->block_cnt == blockfile->head->block_processed;	
		int datafile_exist = IsExist(blockfile->datafilename);	
		if(all_processed && datafile_exist){
			CFilelock lock(blockfile->datafilename);

			if(lock.TryLock()){
				if(chk_md5_sum){
					char md5val[32];
					memset(md5val,0,sizeof(md5val));
					//计算MD5
					ret = fmd5sum(blockfile->datafile, md5val);
					if(ret == 0){
						memcpy(blockfile->head->md5, md5val, 16);
						char md5_hex[44];
						memset(md5_hex,0,sizeof(md5_hex));
						base16_encode(md5val, 16, md5_hex);
						LOG_INFO("file [%s] md5: %s", blockfile->filename, md5_hex);
					}else{
						LOG_ERROR("calc md5(%s) failed! ret=%d", blockfile->datafilename, ret);
					}
				}
				lock.UnLock();
				locked = 1;
			}else{
				LOG_ERROR("lock file[%s] failed!", blockfile->datafilename);
			}
			
		}
		if(blockfile->metafile > 0){
			//fsync(blockfile->metafile);
			close(blockfile->metafile);
			blockfile->metafile = 0;
		}
		if(blockfile->datafile > 0){
			close(blockfile->datafile);
			blockfile->datafile = 0;
		}	

		if(head != NULL){
			memcpy(head, blockfile->head, sizeof(blockfile_head_t));
		}
		ret = munmap(blockfile->head, blockfile->metafilesize);
		if(ret != 0){
			LOG_ERROR("munmap failed! %d err:%s",errno, strerror(errno));
		}
		blockfile->head = NULL;
	}
	if(blockfile->filename != NULL){
		free(blockfile->filename);
		blockfile->filename = NULL;
	}

	free(blockfile);

	if(all_processed && locked==0){//已经由其它进程获取锁，并计算md5
		all_processed = 2;
	}
	errno = 0;
	/**
	 * 返回0 表示关闭成功，但文件未处理完成，需要使用filename.dat访问资源
	 * 返回1 表示关闭成功，并且文件已经处理完成，可以使用filename访问资源。
	 * 返回2 表示关闭成功，并且文件已经处理完成，可以使用filename访问资源。但是是由其它进程rename的。
	 */
	return all_processed;
}

#define CHK_IDX(block_idx, block_cnt); \
	if(block_idx >= block_cnt){ \
		LOG_ERROR("block_index(%d) >= head->block_cnt(%d)", block_idx, block_cnt);\
		return -1;\
	}

int x_block_is_processed(blockfile_t x, const int block_idx)
{
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return 0;
	}
	LOG_DEBUG("block_index: %d", block_idx);

	if(blockfile->head == NULL){
		return 0;
	}
	blockfile_head_t* head = blockfile->head;
	CHK_IDX(block_idx, head->block_cnt);

	return BITTEST(head->bits, block_idx) > 0;
}

int x_block_set_processed(blockfile_t x, const int block_idx)
{
	errno = 0;
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return -1;
	}
	if(blockfile->head == NULL){
		LOG_ERROR("blockfile->head == NULL");
		return -1;
	}
	blockfile_head_t* head = blockfile->head;
	CHK_IDX(block_idx, head->block_cnt);

	if(BITTEST(head->bits, block_idx)==0){
		__sync_add_and_fetch(&head->block_processed,1);
		BITSET(head->bits, block_idx);
	}
	if(blockfile->filename){
		LOG_DEBUG("blockfile:%s, block:%d processed", blockfile->filename, block_idx);
	}
	//立即同步，防止状态丢失，在阿里云主机上，出现过这种情况。
	fsync(blockfile->metafile);

	return 0;
}

int x_head_is_inited(blockfile_t x)
{
	errno = 0;
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return 0;
	}
	
	return blockfile->head!=NULL;
}

#ifdef __APPLE__

int fallocate(int fd, int length)
{
	fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, length};
	// Try to get a continous chunk of disk space
	int ret = fcntl(fd, F_PREALLOCATE, &store);
	if(-1 == ret){
	// OK, perhaps we are too fragmented, allocate non-continuous
	store.fst_flags = F_ALLOCATEALL;
	ret = fcntl(fd, F_PREALLOCATE, &store);
	if (-1 == ret)
	  return ret;
	}
	return ftruncate(fd, length);
}
#endif

EXPORT int x_head_init(blockfile_t x, const int64_t filesize, const int block_size, 
			uint32_t create_time, char* extinfo)
{
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return -1;
	}
	if(block_size<1){
		return -1;
	}
	int blocks = blocks(filesize,block_size);
	//LOG_INFO("x_head_is_inited(x): %d", x_head_is_inited(x));

	if(x_head_is_inited(x)<1){
		int filename_len = strlen(blockfile->filename);
		int tmpfilename_len = strlen(blockfile->datafilename);
		filename_len = MAX(filename_len, tmpfilename_len);
		//创建目录
		int ret = 0;
		char dirname[filename_len];
		if(blockfile->make_dest_dir){
			memset(dirname, 0, filename_len);
			GetFilePath(blockfile->filename, dirname, filename_len);
			ret = ForceMkdir(dirname);
			if(ret != 0){
				LOG_ERROR("ForceMkdir(%s) failed! err:%s", dirname, strerror(errno));
				return -1;
			}
		}

		memset(dirname, 0, filename_len);
		GetFilePath(blockfile->datafilename, dirname, filename_len);
		ret = ForceMkdir(dirname);
		if(ret != 0){
			LOG_ERROR("ForceMkdir(%s) failed! err:%s", dirname, strerror(errno));
			return -1;
		}
		
		blockfile->metafile = open(blockfile->metafilename, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if(blockfile->metafile == -1){
			LOG_ERROR("open metafile [%s] failed! err:%s", blockfile->metafilename, strerror(errno));
			return -1;
		}

		if(IsExist(blockfile->filename)) { //目标文件已经存在，直接打开目标文件
			blockfile->datafile = open(blockfile->filename, O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
			LOG_DEBUG("open localfile [%s] file=%d", blockfile->filename, blockfile->datafile);
		}else{
			blockfile->datafile = open(blockfile->datafilename, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
			LOG_DEBUG("open localfile [%s] file=%d", blockfile->datafilename, blockfile->datafile);
		}
		if(blockfile->datafile == -1){
			LOG_ERROR("open file [%s] failed! error:%s", blockfile->datafilename, strerror(errno));
			close(blockfile->metafile);
			blockfile->metafile = -1;
			return -1;
		}


		int bytes = (blocks+7)/8; 
		int meta_file_size = BLOCKFILE_HEAD_SIZE+bytes;
		
		LOG_DEBUG("blockfile(%s) init filesize:%lld, block_size:%d", 
						blockfile->filename, filesize, block_size);

		#ifdef __APPLE__
		ret = fallocate(blockfile->metafile, meta_file_size);
		#else
		ret = posix_fallocate(blockfile->metafile, 0, meta_file_size);
		#endif

		if(ret != 0){
			close(blockfile->metafile);
			close(blockfile->datafile);
			blockfile->metafile = -1;
			blockfile->datafile = -1;
			LOG_ERROR("posix_fallocate(%s, %d) failed! ret=%d, error:%s",
							blockfile->metafilename, meta_file_size, ret, strerror(errno));
			return -1;
		}
		/**
		ret = ftruncate(blockfile->metafile, meta_file_size);
		if(ret != 0){
			LOG_ERROR("ftruncate(%s, %d) failed! ret=%d, error:%s",
							blockfile->metafilename, meta_file_size, ret, strerror(errno));
			close(blockfile->metafile);
			close(blockfile->datafile);
			blockfile->metafile = -1;
			blockfile->datafile = -1;
			return -1;
		}**/

		void * shm = mmap(NULL, meta_file_size, PROT_READ|PROT_WRITE, MAP_SHARED, 
								blockfile->metafile, 0);
		if(shm == MAP_FAILED){
			if(errno == ENOMEM){
				LOG_ERROR("mmap failed no memory!");
			}else{
				LOG_ERROR("mmap failed! %d err:%s", errno, strerror(errno));
			}
			close(blockfile->metafile);
			close(blockfile->datafile);
			blockfile->metafile = -1;
			blockfile->datafile = -1;
			return -1;
		}

		blockfile->datafilesize = filesize;
		blockfile->metafilesize = meta_file_size;
		blockfile->head = (blockfile_head_t*)shm;
		
		//初始化head
		sprintf(blockfile->head->magic, BLOCKFILE_MAGIC);
		blockfile->head->filesize = filesize;
		blockfile->head->block_size = block_size;
		blockfile->head->block_cnt = blocks;
		//blockfile->head->block_processing = 0;
		//blockfile->head->block_processed = 0;	
		blockfile->head->version = BLOCKFILE_HEAD_VERSION;
	}

	blockfile->head->create_time = create_time;
	if(extinfo != NULL){
		memcpy(blockfile->head->extinfo, extinfo, sizeof(blockfile->head->extinfo)-1);
	}
	// blockfile->head->expires = expires;
	// blockfile->head->last_modified = last_modified;
	// if(etag != NULL){
	// 	strncpy(blockfile->head->etag, etag, sizeof(blockfile->head->etag));
	// }
	//snprintf(blockfile->head->rest, sizeof(blockfile->head->rest)-1, "jie123108@163.com cdn system");

	return 0;
}

EXPORT int x_block_write(blockfile_t x, const int block_idx, const int writed, const char* buf, const int size)
{
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return 0;
	}
	assert(blockfile->head!=NULL);

	off_t offset = (off_t)block_idx * (off_t)blockfile->head->block_size+writed; 
	
	ssize_t write_len = pwrite(blockfile->datafile, buf, size, offset);
	LOG_DEBUG("block_write(file:%d, offset:%d, size:%d)", blockfile->datafile, (int)offset, size);
	if(write_len == -1){
		LOG_ERROR("pwrite(%s, size=%d, offset=%d) failed! %d err:%s", 
				blockfile->datafilename, size, (int)offset, errno, strerror(errno));
	}else{
		#if 0
		int ret = fsync(blockfile->datafile);
		if(ret != 0){
			LOG_ERROR("fsync(%s) failed! %d err:%s", 
				blockfile->datafilename, errno, strerror(errno));
		}
		#endif 
	}
	errno = 0;
	return (int)write_len;
}

EXPORT int x_block_read(blockfile_t x, const int block_idx, char* buf, const int size)
{
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return 0;
	}
	assert(blockfile->head!=NULL);
	off_t offset = (off_t)block_idx * (off_t)blockfile->head->block_size;
	ssize_t ret = pread(blockfile->datafile, buf, size, offset);
	LOG_DEBUG("pread(file:%d, offset:%d, size:%d)", blockfile->datafile, (int)offset, size);
	if(ret == -1){
		LOG_ERROR("pread(%s, size=%d, offset=%d) failed! %d err:%s", blockfile->datafilename, size, (int)offset, errno, strerror(errno));
	}else{
		errno = 0;
	}
	return (int)ret;
}
 
EXPORT blockfile_head_t* x_get_file_head(blockfile_t x)
{
	blockfile_data_t* blockfile = (blockfile_data_t*)x;
	if(blockfile == NULL){
		return NULL;
	}
	return blockfile->head;
}


EXPORT const char* x_so_version()
{
	static char buf[64];
	memset(buf,0, sizeof(buf));
	sprintf(buf, "%s %s", __DATE__, __TIME__);
	return buf;
}

#ifdef METAVIEW

#include <unistd.h>

EXPORT int print_meta_file(const char* filename, int show_detail)
{
	int ret = 0;
	if(access(filename, F_OK)!=0){
		printf("meta file [%s] not exist!\n", filename);
		return -1;
	}

	int metafile = open(filename, O_RDONLY);
	if(metafile == -1){
		printf("open meta file [%s] failed! err: %s\n", filename, strerror(errno));
		return -1;
	}

#define BUF_SIZE (1024*1024)
	char* buf = (char*)malloc(BUF_SIZE);
	memset(buf, 0, BUF_SIZE);
	do{
		int len = read(metafile, buf, BUF_SIZE);
		if(len > 0){
			blockfile_head_t* head = (blockfile_head_t*)buf;
			if(strcmp(head->magic, BLOCKFILE_MAGIC)!=0){
				printf("meta file [%s] invalid! magic error\n", filename);
				ret = -1;
				break;
			} 
			float processed_mb = (float)head->block_processed*head->block_size/1024/1024;
			float processed_percent = (float)head->block_processed*100/head->block_cnt;

			printf("magic: %s, filesize: %lld, processed: %.2fM, percent: %.2f%%\n",
					head->magic, head->filesize, processed_mb, processed_percent);

			printf("block_size: %d, block_count: %d, block_processed: %d,  \n",
					 head->block_size, head->block_cnt, head->block_processed);

			printf("version: %d, create_time: %u\n",
					(int)head->version, head->create_time);
			char md5buf[34];
			memset(md5buf,0, sizeof(md5buf));
			base16_encode(head->md5, 16, md5buf);
			printf(" md5: %s\n", md5buf);

			if(show_detail){
				int i,x,idx;
				int line_cnt = 64;
				for(i=0;i<head->block_cnt;i+=line_cnt){
					char buf_processed_ok[line_cnt+2];
					memset(buf_processed_ok, ' ', line_cnt);
					buf_processed_ok[line_cnt] = 0;
					for(x=0;x<line_cnt;x++){
						idx = i+x;
						if(idx < head->block_cnt){
							int processed_ok = BITTEST(head->bits, idx) > 0;
							buf_processed_ok[x] = processed_ok?'+':'-';
						}
					} 
					int end = i+line_cnt;
					if(end > head->block_cnt){
						end = head->block_cnt;
					}
					printf("%4d ~ %4dm: %s\n", ((uint64_t)i*head->block_size/1024/1024), 
						(((uint64_t)end)*head->block_size/1024/1024), buf_processed_ok);
				}
			}
		}else{
			printf("read meta file [%s] failed! err: %s\n", filename, strerror(errno));
			ret = -1;
		}
	}while(0);
 
	free(buf);
	close(metafile);
	return ret;
}

//#define offsetof(struct_t,member) ((size_t)(char *)&((struct_t *)0)->member)

int main(int argc, char* argv[])
{ 
	printf("---    COMPILE DATA:%s %s\n", __DATE__, __TIME__);
	printf("---   BLOCKFILE_VERSION:%d, HEAD_SIZE: %d, bitmap offset: %d\n", 
			(int)BLOCKFILE_HEAD_VERSION, (int)sizeof(blockfile_head_t), (int)offsetof(blockfile_head_t, bits));

  	#if 0
	printf("--- offsetof(magic): %d\n", (int)offsetof(blockfile_head_t, magic));
	printf("--- offsetof(version): %d\n", (int)offsetof(blockfile_head_t, version));
	printf("--- offsetof(filesize): %d\n", (int)offsetof(blockfile_head_t, filesize));
	printf("--- offsetof(block_size): %d\n", (int)offsetof(blockfile_head_t, block_size));
	printf("--- offsetof(block_cnt): %d\n", (int)offsetof(blockfile_head_t, block_cnt));
	printf("--- offsetof(block_processing): %d\n", (int)offsetof(blockfile_head_t, block_processing));
	printf("--- offsetof(block_processed): %d\n", (int)offsetof(blockfile_head_t, block_processed));
	printf("--- offsetof(md5): %d\n", (int)offsetof(blockfile_head_t, md5));
	printf("--- offsetof(create_time): %d\n", (int)offsetof(blockfile_head_t, create_time));
	printf("--- offsetof(bits): %d\n", (int)offsetof(blockfile_head_t, bits));
  	#endif

	if(argc < 2) { 
		printf("Usage: %s <blockfile metafile.mt> [detail]\n", argv[0]);
		exit(1);
	}  
	int show_detail = 0;
	const char* metafile = argv[1];
	if(argc == 3){
		show_detail = (strcmp(argv[2], "detail")==0);
	}

	int r = print_meta_file(metafile, show_detail);
	exit(r);
}

#endif
