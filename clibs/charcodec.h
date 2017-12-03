#ifndef __CHARCODE_C__
#define __CHARCODE_C__
#include <stdint.h>
#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include <stdlib.h>

typedef struct{
    char enc_tab[64]; //编码字符表
    char dec_tab[256]; //解码表，根据编码表推算出来
}base64_t;

int x_b64_init(base64_t* ctx, const char* enc_tab);
/**
 * base64 编码
 * encoded_data长度必须大于(4.0 * ceil((double) input_length / 3.0))
 * 返回编码后的数据的长度。
 **/
int x_b64_encode(base64_t* ctx, unsigned const char *data, int64_t input_length, char *encoded_data);

/**
 * base64解码
 * input_length长度必须是４的整数倍
 * decoded_data长度必须大于(input_length / 4 * 3)
 * 返回解码后的数据的长度
 */
int x_b64_decode(base64_t* ctx, unsigned const char *data, int64_t input_length, char *decoded_data);

/**
 * url的base64 编码
 * encoded_data长度必须大于(4.0 * ceil((double) input_length / 3.0))
 * 返回编码后的数据的长度。
 **/
int b64_url_encode(unsigned const char *data, int64_t input_length, char *encoded_data);

/**
 * url的base64解码
 * input_length长度必须是４的整数倍
 * decoded_data长度必须大于(input_length / 4 * 3)
 * 返回解码后的数据的长度
 */
int b64_url_decode(unsigned const char *data, int64_t input_length, char *decoded_data);

int base16_decode(const char* base16, int length, char* bin);
const char* base16_encode(const char *text, int length, char* base16);

#ifdef __cplusplus
}
#endif

#endif
