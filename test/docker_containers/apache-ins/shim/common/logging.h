/**
 * @file    logging.h
 * @author  Matt Leinhos
 * @date    14 Feb 2018
 * @version 0.1
 * @brief   MagicWand INS logging API
 *
 * Copyright (C) 2018 Matt Leinhos
 * Copyright (C) 2018 Two Six Labs
 *
 * This file defines the API that the INS uses for logging. Based on
 * https://codereview.stackexchange.com/questions/120715/lightweight-logging-library-in-c
 *
 */

#ifndef _logging_h
#define _logging_h

#ifdef __cplusplus
extern "C" {
#endif

// Logging levels, from least to most verbose
typedef enum {
    LOG_FORCE   =  0,
    LOG_FATAL   = 10,
    LOG_ERROR   = 20,
    LOG_WARN    = 30,
    LOG_NOTICE  = 40,
    LOG_INFO    = 50,
    LOG_DEBUG   = 60,
    LOG_VERBOSE = 70
} log_level;


// Configuration
int
log_init( const char * Path,
          const char * Filename,
          const char * FileExtension,
          log_level    Level );

void log_set_level( log_level Level );
log_level log_get_level( void );

// Logging functions
void log_write( log_level Level, const char* Format, ... );

// Cleanup
void log_flush( void );
void log_close( void );


#ifdef __cplusplus
}
#endif


#endif // _logging_h
