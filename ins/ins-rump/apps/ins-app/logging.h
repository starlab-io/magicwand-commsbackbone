/**
 * @file    logging.h
 * @author  Matt Leinhos
 * @date    14 Feb 2018
 * @version 0.1
 * @brief   MagicWand INS logging API
 *
 * This file defines the API that the INS uses for logging.
 */

#ifndef _logging_h
#define _logging_h

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_FORCE   = 0,
    LOG_ERROR   = 10,
    LOG_WARN    = 20,
    LOG_INFO    = 30,
    LOG_DEBUG   = 40,
    LOG_VERBOSE = 50
} log_level;



/* configuration */
void log_init(const char* path,
              const char* filename,
              const char* file_extension,
              log_level level);
    
log_level log_get_level();

/* logging functions */
void log_write( log_level Level, const char* Format, ...); /* error log */

/* cleanup / ancillary */
void log_flush();
void log_close();


#ifdef __cplusplus
}
#endif


#endif // _logging_h
