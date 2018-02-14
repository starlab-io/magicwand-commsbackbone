/**
 * @file    logging.c
 * @author  Matt Leinhos
 * @date    14 Feb 2018
 * @version 0.1
 * @brief   MagicWand INS logging API
 *
 * This file defines the API that the INS uses for logging.
 */

#include "logging.h"
#include "user_common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h> 
#include <sys/time.h>
#include <time.h>

#define SEPARATOR ('/') // hard-coded Linux path separator

#define MAX_PATH_LENGTH 512
#define MAX_FILENAME 256
#define MAX_FILE_EXTN 20
#define LOG_FILENAME "program"


static log_level loglevel = LOG_WARN;
static char logpath[MAX_PATH_LENGTH] = { 0 };
static char filename[MAX_FILENAME] = LOG_FILENAME;
static char file_extn[MAX_FILE_EXTN] = "log";
static FILE* fp = NULL;

static const char *
get_log_filename()
{
    return filename;
}


static void
set_log_filename(const char* name)
{
    if (name && *name)
        strncpy(filename, name, MAX_FILENAME);
}


static void
set_path(const char* path)
{
    int len;
    if (path && *path != '\0') {
        strncpy(logpath, path, MAX_PATH_LENGTH);
        len = strlen(logpath);
        if (len > 0 && logpath[len - 1] != SEPARATOR)
            logpath[len] = SEPARATOR;
    }
}


static const char *
get_path()
{
    if (!logpath[0])
    {
        sprintf(logpath, ".%c", SEPARATOR);
    }
    return logpath;
}


static char *
get_append_name(char* buf)
{
    time_t now;
    time(&now);
    strftime(buf, 20, "%y%m%d", localtime(&now));
    return buf;
}


static const char *
get_log_filename_extension()
{
    return file_extn[0] ? file_extn : "";
}


static void
set_log_filename_extension(const char* name)
{
    if (name && *name != '\0')
        strncpy(file_extn, name, MAX_FILE_EXTN);
}


static char* construct_full_path(char* path)
{
    char append[20] = { 0 };
    sprintf(path, "%s%s%s.%s", get_path(), get_log_filename(), get_append_name(append), get_log_filename_extension());
    return path;
}


void
log_init( const char* path,
          const char* filename,
          const char* file_extension,
          log_level   level )
{
    char fullpath[MAX_PATH_LENGTH];
    if (path && *path != '\0' && filename && *filename != '\0') {
        set_path(path);
        set_log_filename(filename);
        set_log_filename_extension(file_extension);
        fp = fopen(construct_full_path(fullpath), "a");
        MYASSERT( fp != NULL );
        /* just in case fopen fails, revert to stdout */
        if (fp == NULL) {
            fp = stdout;
            fprintf(fp, "Failed to change logging target\n");
        }
    }
    else
    {
        if (fp != NULL && fp != stdout)
        {
            fclose(fp);
        }

        fp = stdout;
    }

    loglevel = level;    
}


static char *
get_timestamp( char * buf, size_t len )
{
    struct timeval tmnow;
    //   char usec_buf[6];

    gettimeofday(&tmnow, NULL);

    struct tm *tm = localtime(&tmnow.tv_sec);

    size_t bytes = strftime(buf, len, "%H:%M:%S.", tm);

    snprintf( &buf[bytes], len - bytes, "%03d", (int) tmnow.tv_usec );

    return buf;
}


log_level
get_log_level()
{
    return loglevel;
}


void
log_write(log_level level, const char* format, ...)
{
    char tmp[50] = { 0 };
    
    if( level <= loglevel )
    {
        va_list args;
        fprintf( fp, "[%s] ", get_timestamp( tmp, sizeof(tmp) ) );
        va_start (args, format);
        vfprintf (fp, format, args);
        va_end (args);
   }
}


void log_flush( void )
{
    fflush(fp);
}

void log_close( void )
{
    if( fp != stdout )
    {
        fclose(fp);
    }
    fp = NULL;
}
