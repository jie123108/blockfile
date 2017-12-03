#include "pub.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>

#define LOG_BUF_LEN 2048

void ReleasePrint(const char* LEVEL, const char* funcName, 
            const char* fileName, int line,  const char* format,  ...){
    va_list   args;
    va_start(args,   format); 
    char vsp_buf[LOG_BUF_LEN];  
    memset(vsp_buf, 0, LOG_BUF_LEN);
    int pos = 0; 
    pos = snprintf(vsp_buf + pos, LOG_BUF_LEN-pos, "%s:%s[%s:%d] ",
            LEVEL, funcName, fileName, line); 
    pos += vsnprintf(vsp_buf +pos ,  LOG_BUF_LEN-pos, format , args);
    if(pos < LOG_BUF_LEN-1){ 
        pos += snprintf(vsp_buf + pos, LOG_BUF_LEN-pos, "\n");
    }else{
        vsp_buf[LOG_BUF_LEN-2] = '\n';
        vsp_buf[LOG_BUF_LEN-1] = 0;
        pos = LOG_BUF_LEN;
    }
    fprintf(stderr, "%.*s", pos, vsp_buf);
    
    va_end(args); 
}

int fm_system(const char* cmd, char* output, int* len)
{
    FILE *fp;
    int ret = 0;

    /* Open the command for reading. */
    sighandler_t old_handler;  
    old_handler = signal(SIGCHLD, SIG_DFL);  
    fp = popen(cmd, "r");
    if (fp == NULL) {
        signal(SIGCHLD, old_handler);  
        return -1;
    }

    if(output != NULL && len != NULL && *len > 0){
        *len = (int)fread(output, sizeof(char), *len, fp);
    }
    /* close */
    ret = pclose(fp);
    if(ret > 0){
        ret = ret/256;
    }
    signal(SIGCHLD, old_handler);

    return ret;
}
