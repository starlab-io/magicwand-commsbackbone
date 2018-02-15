/**
 * @file    logging.c
 * @author  Matt Leinhos
 * @date    14 Feb 2018
 * @version 0.1
 * @brief   MagicWand INS logging API
 *
 * Copyright (C) 2018 Matt Leinhos
 * Copyright (C) 2018 Two Six Labs
 *
 * This file defines the API that the INS uses for logging.
 */

#include "logging.h"
#include "user_common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include <sys/time.h>
#include <time.h>

//#define SEPARATOR ('/') // hard-coded Linux path separator
#define SEPARATOR ("/") // hard-coded Linux path separator

#define MAX_PATH_LENGTH 512
#define MAX_FILENAME 256
#define MAX_FILE_EXTN 20
#define LOG_FILENAME "program"

#define LOG_DEFAULT_OUTPUT_STREAM stderr


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
set_log_filename(const char* Name)
{
    if (Name && *Name)
    {
        strncpy(filename, Name, MAX_FILENAME);
    }
}


static void
set_path(const char * Path)
{
    if (Path && *Path != '\0')
    {
        strncpy(logpath, Path, MAX_PATH_LENGTH);
    }
}


static const char *
get_path()
{
    if (!logpath[0])
    {
        sprintf(logpath, ".%s", SEPARATOR);
    }
    return logpath;
}


static const char *
get_log_filename_extension()
{
    return file_extn[0] ? file_extn : "";
}


static void
set_log_filename_extension(const char * Name)
{
    if (Name && *Name != '\0')
        strncpy(file_extn, Name, MAX_FILE_EXTN);
}


static char *
construct_full_path(char * Path)
{
    snprintf( Path, MAX_PATH_LENGTH,
              "%s%s%s.%s",
              get_path(),
              SEPARATOR,
              get_log_filename(),
              get_log_filename_extension() );

    return Path;
}


log_level
get_log_level()
{
    return loglevel;
}


void log_set_level( log_level Level )
{
    loglevel = Level;
}


int
log_init( const char * Path,
          const char * Filename,
          const char * FileExtension,
          log_level    Level )
{
    char fullpath[MAX_PATH_LENGTH];
    int rc = 0;
    int err = 0;

    if (Path     && *Path != '\0' &&
        Filename && *Filename != '\0')
    {
        set_path(Path);
        set_log_filename( Filename );
        set_log_filename_extension( FileExtension );

        construct_full_path( fullpath );
        fprintf( LOG_DEFAULT_OUTPUT_STREAM, "Logging to file: %s\n", fullpath );

        fp = fopen( fullpath, "a" );
        if (fp == NULL)
        {
            rc = -1;
            err = errno;
            fprintf( LOG_DEFAULT_OUTPUT_STREAM, "Failed to open logging target.\n" );
            goto ErrorExit;
        }

    }
    else
    {
        if (fp != NULL && fp != LOG_DEFAULT_OUTPUT_STREAM )
        {
            fclose(fp);
        }
        fprintf( LOG_DEFAULT_OUTPUT_STREAM, "Logging to stderr\n" );
        fp = LOG_DEFAULT_OUTPUT_STREAM;
    }

    log_set_level( Level );

ErrorExit:
    errno = err;
    return rc;
}


static char *
get_timestamp( char * Buf, size_t Len )
{
    struct timeval tmnow;

    gettimeofday(&tmnow, NULL);

    struct tm *tm = localtime(&tmnow.tv_sec);

    size_t bytes = strftime(Buf, Len, "%H:%M:%S.", tm);

    snprintf( &Buf[bytes], Len - bytes, "%03d", (int) tmnow.tv_usec );

    return Buf;
}


void
log_write(log_level Level, const char* Format, ...)
{
    char tmp[50] = { 0 };
    
    if( Level <= loglevel )
    {
        va_list args;
        fprintf( fp, "[%s] <%d>", get_timestamp( tmp, sizeof(tmp) ), Level );
        va_start (args, Format);
        vfprintf (fp, Format, args);
        va_end (args);
   }
}


void log_flush( void )
{
    fflush(fp);
}


void log_close( void )
{
    if( fp != LOG_DEFAULT_OUTPUT_STREAM )
    {
        fclose(fp);
    }
    fp = NULL;
}
