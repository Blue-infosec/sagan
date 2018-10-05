/*
** Copyright (C) 2009-2018 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2018 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* after.c - Logic for "after" in Sagan rule */

/* TODO:  Need to test IPC limits for threshold/after/client tracking */


#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-config.h"
#include "rules.h"
#include "after.h"
#include "ipc.h"

pthread_mutex_t After_By_Src_Mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t After_By_Dst_Mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t After_By_Src_Port_Mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t After_By_Dst_Port_Mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t After_By_Username_Mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t After2_Mutex=PTHREAD_MUTEX_INITIALIZER;


struct after_by_src_ipc *afterbysrc_ipc;
struct after_by_dst_ipc *afterbydst_ipc;
struct after_by_srcport_ipc *afterbysrcport_ipc;
struct after_by_dstport_ipc *afterbydstport_ipc;
struct after_by_username_ipc *afterbyusername_ipc;

struct _After2_IPC *After2_IPC;

struct _SaganCounters *counters;
struct _Rule_Struct *rulestruct;
struct _SaganDebug *debug;
struct _SaganConfig *config;

struct _Sagan_IPC_Counters *counters_ipc;

bool After2 ( int rule_position, char *ip_src, uint32_t src_port, char *ip_dst,  uint32_t dst_port, char *username, char *selector, char *syslog_message )
{

    time_t t;
    struct tm *now;
    char  timet[20];

    int i;

    uint64_t after_oldtime;

    t = time(NULL);
    now=localtime(&t);
    strftime(timet, sizeof(timet), "%s",  now);

    char src_tmp[MAXIP] = { 0 };
    char dst_tmp[MAXIP] = { 0 };
    char username_tmp[MAX_USERNAME_SIZE] = { 0 };

    char hash_string[128] = { 0 };
    char debug_string[64] = { 0 };

    uint32_t hash;

    bool after_log_flag = true;

    username_tmp[0] = '\0';

    if ( rulestruct[rule_position].after2_method_src == true )
        {
            strlcpy(src_tmp, ip_src, sizeof(src_tmp));
        }

    if ( rulestruct[rule_position].after2_method_dst == true )
        {
            strlcpy(dst_tmp, ip_dst, sizeof(dst_tmp));
        }

    if ( rulestruct[rule_position].after2_method_username == true && username != NULL )
        {
            strlcpy(username_tmp, username, sizeof(username_tmp));
        }

    snprintf(hash_string, sizeof(hash_string), "%s|%d|%s|%d|%s", src_tmp, src_port, dst_tmp, dst_port, username_tmp);

    hash = Djb2_Hash( hash_string );

    for (i = 0; i < counters_ipc->after2_count; i++ )
        {

            if ( hash == After2_IPC[i].hash &&
                    !strcmp(After2_IPC[i].sid, rulestruct[rule_position].s_sid) &&
                    ( selector == NULL || !strcmp(selector, After2_IPC[i].selector)) )
                {

                    File_Lock(config->shm_after2);
                    pthread_mutex_lock(&After2_Mutex);

                    After2_IPC[i].count++;
                    After2_IPC[i].total_count++;

                    after_oldtime = atol(timet) - After2_IPC[i].utime;

                    strlcpy(After2_IPC[i].syslog_message, syslog_message, sizeof(After2_IPC[i].syslog_message));
                    strlcpy(After2_IPC[i].signature_msg, rulestruct[rule_position].s_msg, sizeof(After2_IPC[i].signature_msg));

                    /* Reset counter if it's expired */

                    if ( after_oldtime > rulestruct[rule_position].after2_seconds ||
                            After2_IPC[i].count == 0 )
                        {

                            After2_IPC[i].count=1;
                            After2_IPC[i].utime = atol(timet);

                            after_log_flag = true;
                        }


                    if ( rulestruct[rule_position].after2_count < After2_IPC[i].count )
                        {

                            after_log_flag = false;

                            if ( debug->debuglimits )
                                {

                                    if ( After2_IPC[i].after2_method_src == true )
                                        {
                                            strlcat(debug_string, "by_src ", sizeof(debug_string));
                                        }

                                    if ( After2_IPC[i].after2_method_dst == true )
                                        {
                                            strlcat(debug_string, "by_dst ", sizeof(debug_string));
                                        }

                                    if ( After2_IPC[i].after2_method_username == true )
                                        {
                                            strlcat(debug_string, "by_username ", sizeof(debug_string));
                                        }

                                    Sagan_Log(NORMAL, "After SID %s. Tracking by %s[Hash: %lu]", After2_IPC[i].sid, debug_string, hash);

                                }

                            counters->after_total++;
                        }

                    pthread_mutex_unlock(&After2_Mutex);
                    File_Unlock(config->shm_after2);

                    return(after_log_flag);
                }

        }


    /* If not found add it to the array */

    if ( Clean_IPC_Object(AFTER2) == 0 )
        {

            File_Lock(config->shm_after2);
            pthread_mutex_lock(&After2_Mutex);

            After2_IPC[counters_ipc->after2_count].hash = hash;

            selector == NULL ? After2_IPC[counters_ipc->after2_count].selector[0] = '\0' : strlcpy(After2_IPC[counters_ipc->after2_count].selector, selector, MAXSELECTOR);

            After2_IPC[counters_ipc->after2_count].count = 1;
            After2_IPC[counters_ipc->after2_count].utime = atol(timet);
            After2_IPC[counters_ipc->after2_count].expire = rulestruct[rule_position].after2_seconds;

            After2_IPC[counters_ipc->after2_count].after2_method_src = rulestruct[rule_position].after2_method_src;
            After2_IPC[counters_ipc->after2_count].after2_method_dst = rulestruct[rule_position].after2_method_dst;
            After2_IPC[counters_ipc->after2_count].after2_method_username = rulestruct[rule_position].after2_method_username;

            strlcpy(After2_IPC[counters_ipc->after2_count].ip_src, src_tmp, sizeof(After2_IPC[counters_ipc->after2_count].ip_src));
            strlcpy(After2_IPC[counters_ipc->after2_count].ip_dst, dst_tmp, sizeof(After2_IPC[counters_ipc->after2_count].ip_dst));
            strlcpy(After2_IPC[counters_ipc->after2_count].username, username_tmp, sizeof(After2_IPC[counters_ipc->after2_count].username));

            strlcpy(After2_IPC[counters_ipc->after2_count].sid, rulestruct[rule_position].s_sid, sizeof(After2_IPC[counters_ipc->after2_count].sid));
            strlcpy(After2_IPC[counters_ipc->after2_count].syslog_message, syslog_message, sizeof(After2_IPC[counters_ipc->after2_count].syslog_message));
            strlcpy(After2_IPC[counters_ipc->after2_count].signature_msg, rulestruct[rule_position].s_msg, sizeof(After2_IPC[counters_ipc->after2_count].signature_msg));

            counters_ipc->after2_count++;

            pthread_mutex_unlock(&After2_Mutex);
            File_Unlock(config->shm_after2);
        }

    return(true);
}

