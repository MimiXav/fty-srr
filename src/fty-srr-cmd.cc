/*  =========================================================================
    fty-srr-cmd - Binary

    Copyright (C) 2014 - 2019 Eaton

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

/*
@header
    fty-srr-cmd - Binary
@discuss
@end
*/

#include "fty_srr_classes.h"

#include <iostream>

#define END_POINT                       "ipc://@/malamute"
#define AGENT_NAME                      "fty-srr-cmd"
#define AGENT_NAME_REQUEST_DESTINATION  "fty-srr"
#define MSG_QUEUE_NAME                  "ETN.Q.IPMCORE.SRR"
#define DEFAULT_TIME_OUT                5

using namespace dto::srr;

dto::UserData sendRequest(const std::string & action, const dto::UserData& userData);

int main (int argc, char *argv [])
{
     GOOGLE_PROTOBUF_VERIFY_VERSION;

     try
     {        
        // Default status
        Query query = createListFeatureQuery();

        dto::UserData reqData;
        reqData << query;

        // Send request
        dto::UserData respData = sendRequest("get", reqData);
        if (respData.empty())
        {
           throw std::runtime_error("Impossible to get the list of features.");
        }

        Response response;
        respData >> response;

        std::cout << responseToUiJson(response, true) << std::endl;
     }
     catch(std::exception & e)
     {
         std::cerr << e.what() << std::endl;
         return 1;
     }
     
    return 0;
}

dto::UserData sendRequest(const std::string & action, const dto::UserData& userData)
{
    log_debug("sendRequest <%s> action");
    // Client id
    std::string clientId = messagebus::getClientId(AGENT_NAME);
    std::unique_ptr<messagebus::MessageBus> requester(messagebus::MlmMessageBus(END_POINT, clientId));
    requester->connect();

    // Build message
    messagebus::Message msg;
    msg.userData() = userData;
    msg.metaData().emplace(messagebus::Message::SUBJECT, action);
    msg.metaData().emplace(messagebus::Message::FROM, clientId);
    msg.metaData().emplace(messagebus::Message::TO, AGENT_NAME_REQUEST_DESTINATION);
    msg.metaData().emplace(messagebus::Message::COORELATION_ID, messagebus::generateUuid());
    // Send request
    messagebus::Message resp = requester->request(MSG_QUEUE_NAME, msg, DEFAULT_TIME_OUT);
    // Return the data response
    return resp.userData();
}
