/*
** Copyright (C) 2009-2015 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2015 Champ Clark III <cclark@quadrantsec.com>
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

/* sagan-track-clients.c
*
* Simple pre-processors that keeps track of reporting syslog clients/agents.
* This is based off the IP address the clients,  not based on normalization.
* If a client/agent hasn't sent a syslog/event message in X minutes,  then
* generate an alert.
*
*/

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-send-alert.h"
#include "sagan-track-clients.h"
#include "sagan-config.h"

struct _Sagan_Track_Clients *SaganTrackClients;
struct _SaganCounters *counters;
struct _SaganConfig *config;
struct _Sagan_Processor_Info *processor_info_track_client = NULL;

pthread_mutex_t SaganProcTrackClientsMutex=PTHREAD_MUTEX_INITIALIZER;

struct _Sagan_Proc_Syslog *SaganProcSyslog;

void Sagan_Track_Clients_Init ( void )
{

    SaganTrackClients = malloc(sizeof(_Sagan_Track_Clients));

    if ( SaganTrackClients == NULL )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to allocate memory for SaganTrackClients. Abort!", __FILE__, __LINE__);
        }

    memset(SaganTrackClients, 0, sizeof(_Sagan_Track_Clients));

}

void Sagan_Load_Tracking_Cache ( void )
{

    char track_buf[1024] = { 0 };

    char  timet[20] = { 0 };
    time_t t;
    struct tm *now;

    sbool client_track_flag = 0;

    t = time(NULL);
    now=localtime(&t);
    strftime(timet, sizeof(timet), "%s",  now);

    processor_info_track_client = malloc(sizeof(struct _Sagan_Processor_Info));

    if ( processor_info_track_client == NULL )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to allocate memory for processor_info_track_client. Abort!", __FILE__, __LINE__);
        }

    memset(processor_info_track_client, 0, sizeof(_Sagan_Processor_Info));

    processor_info_track_client->processor_name         =       PROCESSOR_NAME;
    processor_info_track_client->processor_generator_id =       PROCESSOR_GENERATOR_ID;
    processor_info_track_client->processor_name         =       PROCESSOR_NAME;
    processor_info_track_client->processor_facility     =       PROCESSOR_FACILITY;
    processor_info_track_client->processor_priority     =       PROCESSOR_PRIORITY;
    processor_info_track_client->processor_pri          =       PROCESSOR_PRI;
    processor_info_track_client->processor_class                =       PROCESSOR_CLASS;
    processor_info_track_client->processor_tag          =       PROCESSOR_TAG;
    processor_info_track_client->processor_rev          =       PROCESSOR_REV;

    if (( config->sagan_track_client_file = fopen(config->sagan_track_client_host_cache, "r" )) == NULL )
        {
            Sagan_Log(S_WARN, "Client Tracking cache not found, creating %s.", config->sagan_track_client_host_cache);
            client_track_flag = 1;
        }


    if (client_track_flag == 0 )
        {
            while(fgets(track_buf, 1024, config->sagan_track_client_file) != NULL)
                {

                    /* Skip comments and blank linkes */

                    if (track_buf[0] == '#' || track_buf[0] == 10 || track_buf[0] == ';' || track_buf[0] == 32)
                        {
                            continue;
                        }
                    else
                        {
                            /* Allocate memory for tracking clients cache */

                            pthread_mutex_lock(&SaganProcTrackClientsMutex);

                            SaganTrackClients = (_Sagan_Track_Clients *) realloc(SaganTrackClients, (counters->track_clients_client_count+1) * sizeof(_Sagan_Track_Clients));
                            strlcpy(SaganTrackClients[counters->track_clients_client_count].host, Remove_Return(track_buf), sizeof(SaganTrackClients[counters->track_clients_client_count].host));
                            SaganTrackClients[counters->track_clients_client_count].utime = atol(timet);
                            SaganTrackClients[counters->track_clients_client_count].status = 0;
                            counters->track_clients_client_count++;

                            pthread_mutex_unlock(&SaganProcTrackClientsMutex);

                        }
                }
            Sagan_Log(S_NORMAL, "Client Tracking loaded %d host(s) to track", counters->track_clients_client_count);
        }

    if (( config->sagan_track_client_file = fopen(config->sagan_track_client_host_cache, "a" )) == NULL )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open client tracking cache file for writing (%s)", __FILE__, __LINE__, config->sagan_track_client_host_cache);
        }

    if (client_track_flag == 1 )
        {
            fprintf(config->sagan_track_client_file, "# This file is automatically created by Sagan\n");
        }

}


int Sagan_Track_Clients ( _SaganProcSyslog *SaganProcSyslog_LOCAL )
{

    int alertid;

    char  timet[20] = { 0 };
    time_t t;
    struct tm *now;

    int i;
    sbool tracking_flag=0;

    long utimetmp;

    t = time(NULL);
    now=localtime(&t);
    strftime(timet, sizeof(timet), "%s",  now);

    for (i=0; i<counters->track_clients_client_count; i++)
        {

            if (!strcmp(SaganProcSyslog_LOCAL->syslog_host, SaganTrackClients[i].host))
                {

                    SaganTrackClients[i].utime = atol(timet);

                    /* Logs being received */

                    if ( SaganTrackClients[i].status == 1 )
                        {

                            Sagan_Log(S_WARN, "[Processor: %s] Logs being received from %s again.",  PROCESSOR_NAME, SaganTrackClients[i].host);
                            snprintf(SaganProcSyslog_LOCAL->syslog_message, sizeof(SaganProcSyslog_LOCAL->syslog_message)-1, "The IP address %s was previous reported as being down or not sending logs.  The system appears to be sending logs again", SaganTrackClients[i].host);
                            counters->track_clients_down--;

                            alertid=101;
                            SaganTrackClients[i].status = 0;
                            Sagan_Send_Alert(SaganProcSyslog_LOCAL, processor_info_track_client, SaganTrackClients[i].host, config->sagan_host, config->sagan_proto, alertid, config->sagan_port, config->sagan_port, 0);
                        }

                    tracking_flag=1;
                }

            utimetmp = SaganTrackClients[i].utime ;

            /* Logs stop being received */

            if ( atol(timet) - utimetmp >  config->pp_sagan_track_clients * 60 && SaganTrackClients[i].status == 0 )
                {

                    counters->track_clients_down++;

                    Sagan_Log(S_WARN, "[Processor: %s] Logs have not been seen from %s for %d minute(s).", PROCESSOR_NAME, SaganTrackClients[i].host, config->pp_sagan_track_clients);
                    snprintf(SaganProcSyslog_LOCAL->syslog_message, sizeof(SaganProcSyslog_LOCAL->syslog_message)-1, "Sagan has not recieved any logs from the IP address %s in over %d minute(s). This could be an indication that the system is down.", SaganTrackClients[i].host, config->pp_sagan_track_clients);

                    alertid=100;
                    SaganTrackClients[i].status = 1;

                    Sagan_Send_Alert(SaganProcSyslog_LOCAL, processor_info_track_client, SaganTrackClients[i].host, config->sagan_host, config->sagan_proto, alertid, config->sagan_port, config->sagan_port, 0);
                }

        }

    if ( tracking_flag == 0)
        {

            SaganTrackClients = (_Sagan_Track_Clients *) realloc(SaganTrackClients, (counters->track_clients_client_count+1) * sizeof(_Sagan_Track_Clients));
            strlcpy(SaganTrackClients[counters->track_clients_client_count].host, SaganProcSyslog_LOCAL->syslog_host, sizeof(SaganTrackClients[counters->track_clients_client_count].host));
            SaganTrackClients[counters->track_clients_client_count].utime = atol(timet);
            SaganTrackClients[counters->track_clients_client_count].status = 0;
            fprintf(config->sagan_track_client_file, "%s\n", SaganTrackClients[counters->track_clients_client_count].host);
            counters->track_clients_client_count++;

        }

    return(0);
}

