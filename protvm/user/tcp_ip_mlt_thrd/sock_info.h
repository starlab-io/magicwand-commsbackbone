/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    sock_info.h
 * @author  Mark Mason 
 * @date    4 November 2016
 * @version 0.1
 * @brief   Sock information is collected into a struct, and maintained in
 * a list by a list manager.
 */

#ifndef __SOCK_INFO_H__
#define __SOCK_INFO_H__

typedef struct _sinfo {
    int    sockfd;
    char * desthost;
    int    destport;
} sinfo_t;

sinfo_t sock_info;

#endif

