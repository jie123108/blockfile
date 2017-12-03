#ifndef _PUB_H__
#define _PUB_H__

#ifdef __cplusplus
extern "C"{
#endif

void ReleasePrint(const char* LEVEL, const char* funcName, 
            const char* fileName, int line,  const char* format,  ...);
int fm_system(const char* cmd, char* output, int* len);

#ifdef DEBUG 
#define LOG_DEBUG(format, args...) \
        ReleasePrint("DEBUG", __FUNCTION__, __FILE__, __LINE__, format, ##args)
#else
#define LOG_DEBUG(format, args...) 
#endif

#define LOG_INFO(format, args...) \
        ReleasePrint(" INFO", __FUNCTION__, __FILE__, __LINE__, format, ##args)
#define LOG_WARN(format, args...) \
        ReleasePrint(" WARN", __FUNCTION__, __FILE__, __LINE__, format, ##args)
#define LOG_ERROR(format, args...) \
        ReleasePrint("ERROR", __FUNCTION__, __FILE__, __LINE__, format, ##args)

#ifdef __cplusplus
}
#endif

#endif
