/*  =========================================================================
    fty-srr - Save and restore and reset agent for 42ITy ecosystem

    Copyright (C) 2014 - 2018 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

#ifndef FTY_SRR_H_H_INCLUDED
#define FTY_SRR_H_H_INCLUDED

//  Include the project library file
#include "fty_srr_library.h"

//  SRR agent configuration
#define AGENT_NAME_KEY                     "agentName"
#define AGENT_NAME                         "fty-srr"
#define ENDPOINT_KEY                       "endPoint"
#define DEFAULT_ENDPOINT                   "ipc://@/malamute"
#define DEFAULT_LOG_CONFIG                 "/etc/fty/ftylog.cfg"
#define SRR_QUEUE_NAME_KEY                 "queueName"
#define SRR_MSG_QUEUE_NAME                 "ETN.Q.IPMCORE.SRR"
// Config agent definition
#define CONFIG_AGENT_NAME                  "fty-config"
#define CONFIG_MSG_QUEUE_NAME              "ETN.Q.IPMCORE.CONFIG"
// EMC4J agent definition
#define EMC4J_AGENT_NAME                   "etn-malamute-translator"
#define EMC4J_MSG_QUEUE_NAME               "ETN.Q.EMC4J.REQ"
// Action definition
#define GET_ACTION                         "getFeatureList"
#define SAVE_ACTION                        "save"
#define RESTORE_ACTION                     "restore"
#define RESET_ACTION                       "reset"
// Status definition
#define STATUS_SUCCESS                     "success"
#define STATUS_FAILED                      "failed"
#define STATUS_PARTIAL_SUCCESS             "partialSuccess"
#define STATUS_UNKNOWN                     "unknown"
// Common definition
#define SRR_VERSION_KEY                    "version"
#define SRR_VERSION                        "version"
#define ACTIVE_VERSION                     "1.0"
#define FEATURE_LIST_NAME                  "featuresList"
#define FEATURE_NAME                       "name"
#define DATA_MEMBER                        "data"

#endif
