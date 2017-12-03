#include "charcodec.h"
#include "pub.h"
#include <math.h>
#include <string.h>


//ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_
static uint8_t valid_base64_char[256] =
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,
0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static int mod_table[] = {0, 2, 1};

static base64_t url_b64_ctx;

__attribute__((constructor(101))) static void __standard_base64_init() {
    memset(&url_b64_ctx,0,sizeof(base64_t));
    x_b64_init(&url_b64_ctx, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}
int b64_url_encode(unsigned const char *data, int64_t input_length, char *encoded_data)
{
    return x_b64_encode(&url_b64_ctx, data, input_length, encoded_data);
}

int b64_url_decode(unsigned const char *data, int64_t input_length, char *decoded_data)
{
    return x_b64_decode(&url_b64_ctx, data, input_length, decoded_data);
}

int x_b64_init(base64_t* ctx, const char* enc_tab)
{
    memset(ctx, 0, sizeof(base64_t));

    int len = strlen(enc_tab);
    if(len != 64){
        LOG_ERROR("enc_tab [%s] invalid! len(%d) != 64", enc_tab, len);
        return -1;
    }
    char flags[256];
    memset(flags,0, sizeof(flags));

    //验证enc_tab合法性。
    int i;
    for(i=0;i<len;i++){
        char c = enc_tab[i];
        if(flags[c]>0) {//字符c出现了两次。
            LOG_ERROR("a repeated character '%c'(hex:0x%02x) string in enc_tab \"%s\"[%d]", c, c, enc_tab, i);
            return -1;
        }else{
            flags[c] = 1;
        }
    }
    memcpy(ctx->enc_tab, enc_tab, len);
    for (i = 0; i < 0x40; i++){
        ctx->dec_tab[ctx->enc_tab[i]] = i;
    }

    return 0;
}
/**
 * base64 编码
 * encoded_data长度必须大于(4.0 * ceil((double) input_length / 3.0))
 * 返回编码后的数据的长度。
 **/
int x_b64_encode(base64_t* ctx, unsigned const char *data, int64_t input_length, char *encoded_data)
{

    int output_length = (int64_t) (4.0 * ceil((double) input_length / 3.0));
    for (int i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = ctx->enc_tab[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = ctx->enc_tab[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = ctx->enc_tab[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = ctx->enc_tab[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[output_length - 1 - i] = '=';

    return output_length;
}

/**
 * base64解码
 * input_length长度必须是４的整数倍
 * decoded_data长度必须大于(input_length / 4 * 3)
 * 返回解码后的数据的长度
 */
int x_b64_decode(base64_t* ctx, unsigned const char *data, int64_t input_length, char *decoded_data)
{
    if (input_length % 4 != 0) return -1;
    int output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=') output_length--;
    if (data[input_length - 2] == '=') output_length--;
    
    for (int i = 0, j = 0; i < input_length;) {
        uint32_t sextet_a = data[i] == '=' ? 0 & i++ : ctx->dec_tab[data[i++]];
        uint32_t sextet_b = data[i] == '=' ? 0 & i++ : ctx->dec_tab[data[i++]];
        uint32_t sextet_c = data[i] == '=' ? 0 & i++ : ctx->dec_tab[data[i++]];
        uint32_t sextet_d = data[i] == '=' ? 0 & i++ : ctx->dec_tab[data[i++]];

        uint32_t triple = (sextet_a << 3 * 6)
                        + (sextet_b << 2 * 6)
                        + (sextet_c << 1 * 6)
                        + (sextet_d << 0 * 6);

        if (j < output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
    }

    return output_length;
}

const char* base16_encode(const char *text, int length, char* base16)
{
    int i;
    static const char hextable[20] = "0123456789abcdef";

    for(i = 0; i < length; i++){
        base16[i*2] = hextable[(text[i]&0xf0)>>4];
        base16[i*2+1] = hextable[text[i]&0xf];
    }
    return base16;
}

#define BAD -1
const char base16val[128] = {
    BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
    BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
    BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
      0,  1,  2,  3,   4,  5,  6,  7,   8,  9,BAD,BAD, BAD,BAD,BAD,BAD,
    BAD, 10, 11, 12,  13, 14, 15,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
    BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
    BAD, 10, 11, 12,  13, 14, 15,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
    BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
};

#define decode16(ch)((unsigned char)ch < 128 ? base16val[ch]:BAD)

int base16_decode(const char* base16, int length, char* bin)
{
    if (length % 2 != 0)
        return -1;
    int i=0;
    for (i = 0; i < length; i += 2) {
        // 高四位
        char ch = decode16(base16[i]);
        if (ch ==BAD)
            return -1;  // 非法字符
        bin[i / 2] = ch << 4;
 
        // 低四位
        ch = decode16(base16[i + 1]);
        if (ch == BAD)
            return -1;  // 非法字符
        bin[i/2] |= ch;
    }
    return 0;
}
