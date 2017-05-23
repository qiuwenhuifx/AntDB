#ifndef ADBLOADERLOG
#define ADBLOADERLOG

typedef enum LOG_TYPE
{
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
} LOG_TYPE;

extern FILE *adbloader_log_file_fd;
extern void adbLoader_log_init(char *logfilepath, LOG_TYPE type);
extern void adbLoader_log_end(void);
extern void adbloader_log_type(LOG_TYPE type,
								const char *file,
								const char *fun,
								int line,
								const char *fmt, ...)
__attribute__((format(PG_PRINTF_ATTRIBUTE, 5, 6)));

#define ADBLOADER_LOG(type, fmt, args...) \
	adbloader_log_type(type, __FILE__, __FUNCTION__, __LINE__, fmt, ##args)

//==================================================================

extern FILE *adb_load_error_data_fd;
extern void fopen_adb_load_error_data(char *path, char *tablename);
extern void fwrite_adb_load_error_data(char *linedata);
extern void fclose_adb_load_error_data(void);
extern void check_db_load_error_data(char *path, char *tablename);

#endif