
CC=g++
all: libblockfile.so metaview
CFLAGS=-g -D_FILE_OFFSET_BITS=64 -D_LARGE_FILE -Werror -fPIC -I c
# LDFLAGS=-lcrypto

SRCS=blockfile.c charcodec.c pub.c md5.c
SRCS_CRYPT=file_crypt/file_main.cpp file_crypt/file_crypt.cpp pub.c

libblockfile.so: ${SRCS} 
	$(CC) ${CFLAGS} ${LDFLAGS} -Werror $^ -fPIC -shared -o $@

debug: ${OBJS}
	$(CC) -DDEBUG ${CFLAGS} ${LDFLAGS} $^ -fPIC -shared -o libblockfile.so

metaview: ${SRCS}
	$(CC) ${CFLAGS} ${LDFLAGS} -DMETAVIEW $^ -o $@

block_chksum: ${SRCS} block_chksum.c
	$(CC) ${CFLAGS} ${LDFLAGS} $^ -o $@


clean:
	rm -f libblockfile.so metaview *.o *.oxx 