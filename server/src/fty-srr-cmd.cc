/*  =========================================================================
    fty-srr-cmd - Binary

    Copyright (C) 2014 - 2020 Eaton

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

#include "dto/request.h"
#include "dto/response.h"


#include <cxxtools/serializationinfo.h>
#include <fty/command-line.h>
#include <fty_common.h>
#include <fty_common_dto.h>
#include <fty_common_messagebus.h>
#include <fty_log.h>
#include <fty/split.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define END_POINT "ipc://@/malamute"
#define AGENT_NAME "fty-srr-cmd"
#define AGENT_NAME_REQUEST_DESTINATION "fty-srr-ui"
#define MSG_QUEUE_NAME "ETN.Q.IPMCORE.SRR.UI"
#define DEFAULT_TIME_OUT 300

using namespace dto::srr;

template <typename T>
std::ostream &operator<< (std::ostream &os, const std::vector<T> &vec)
{
    auto it = vec.begin();
    for(; it != vec.end() -1; it++) {
        os << *it << ", ";
    }
    os << *it;
    return os;
}

dto::UserData sendRequest (const std::string &action,
                           const dto::UserData &userData);

// operations
std::vector<std::string> opList (void);
void opSave (const std::string &passphrase,
             const std::vector<std::string> &groupList,
             std::ostream &os);
void opRestore (const std::string &passphrase, std::istream &is);
void opReset (void);

int main (int argc, char **argv)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    bool help = false;

    std::string operation;
    std::string fileName;
    std::string groups;
    std::string passphrase;

    // clang-format off
    fty::CommandLine cmd("SRR command line", {
        {"--help|-h", help, "Show this help"},
        {"--operation|-o", operation, "comment"},
        {"--passphrase|-p", passphrase, "comment"},
        {"--groups|-g", groups, "comment"},
        {"--file|-f", fileName, "comment"}
    });

    cmd.option("--operation").setOneOfMany({"list", "save", "restore", "reset"});

    if (auto res = cmd.parse(argc, argv); !res) {
        std::cerr << res.error() << std::endl;
        std::cout << std::endl;
        std::cout << cmd.help() << std::endl;
        return EXIT_FAILURE;
    }

    if(help) {
        std::cout << cmd.help() << std::endl;
        return EXIT_SUCCESS;
    }

    if(operation.empty()) {
        std::cout << "No operation specified" << std::endl;
        std::cout << cmd.help() << std::endl;
        return EXIT_FAILURE;
    }

    if(operation == "list") {
        auto list = opList();
        list.clear();
    } else if(operation == "save") {
        if(passphrase.empty()) {
            std::cerr << "Passphrase is required with save operation" << std::endl;
            std::cout << cmd.help() << std::endl;
            return EXIT_FAILURE;
        }
        std::ofstream outputFile;
        if(!fileName.empty()) {
            try{
                outputFile.open(fileName);
            } catch(const std::exception& e) {
                std::cerr << "Can't open output file: " << e.what() << std::endl;
                return EXIT_FAILURE;
            }
        }
        std::vector<std::string> groupList;
        if(!groups.empty()) {
            groupList = fty::split(groups, ",", fty::SplitOption::Trim);
            std::cout << "Saving groups: " << groupList << std::endl;
        } else {
            std::cout << "No group option specified\nSaving all groups" << std::endl;
            groupList = opList();
        }
        opSave(passphrase, groupList, outputFile.is_open() ? outputFile : std::cout);
        if(outputFile.is_open()) {
            outputFile.close();
        }
    } else if(operation == "restore") {
        if(passphrase.empty()) {
            std::cerr << "Passphrase is required with restore operation" << std::endl;
            std::cout << cmd.help() << std::endl;
            return EXIT_FAILURE;
        }
        std::ifstream inputFile;
        if(!fileName.empty()) {
            try{
                inputFile.open(fileName);
            } catch(const std::exception& e) {
                std::cerr << "Can't open input file: " << e.what() << std::endl;
                return EXIT_FAILURE;
            }
        }
        opRestore(passphrase, inputFile.is_open() ? inputFile : std::cin);
        if(inputFile.is_open()) {
            inputFile.close();
        }
    } else if(operation == "reset") {
        opReset();
    } else {
        std::cout << "unknown" << std::endl;
    }

    return EXIT_SUCCESS;
}

dto::UserData sendRequest (const std::string &action,
                           const dto::UserData &userData)
{
    log_debug ("sendRequest <%s> action", action.c_str());
    // Client id
    std::string clientId = messagebus::getClientId (AGENT_NAME);
    std::unique_ptr<messagebus::MessageBus> requester (
      messagebus::MlmMessageBus (END_POINT, clientId));
    requester->connect ();

    // Build message
    messagebus::Message msg;
    msg.userData () = userData;
    msg.metaData ().emplace (messagebus::Message::SUBJECT, action);
    msg.metaData ().emplace (messagebus::Message::FROM, clientId);
    msg.metaData ().emplace (messagebus::Message::TO,
                             AGENT_NAME_REQUEST_DESTINATION);
    msg.metaData ().emplace (messagebus::Message::CORRELATION_ID,
                             messagebus::generateUuid ());
    // Send request
    messagebus::Message resp =
      requester->request (MSG_QUEUE_NAME, msg, DEFAULT_TIME_OUT);
    // Return the data response
    return resp.userData ();
}

std::vector<std::string> opList() {
    std::vector<std::string> groupList;

    try {
        dto::UserData reqData;

        // Send request
        dto::UserData respData = sendRequest ("list", reqData);
        if (respData.empty ()) {
            throw std::runtime_error (
              "Impossible to get the list of features");
        }

        srr::SrrListResponse resp;

        cxxtools::SerializationInfo si;
        JSON::readFromString(respData.front(), si);

        si >>= resp;

        std::cout << " Groups available:" << std::endl;
        for(auto g : resp.m_groups) {
            std::cout << " - " << g.m_group_id << std::endl;
            groupList.push_back(g.m_group_id);
        }
    }
    catch (std::exception &e) {
        std::cerr << e.what () << std::endl;
    }

    return groupList;
}

void opSave(const std::string& passphrase, const std::vector<std::string>& groupList, std::ostream& os) {
    srr::SrrSaveRequest req;
    req.m_group_list = groupList;
    req.m_passphrase = passphrase;

    cxxtools::SerializationInfo reqSi;

    reqSi <<= req;

    try {
        dto::UserData reqData;
        reqData.push_back(JSON::writeToString(reqSi, false));

        // Send request
        dto::UserData respData = sendRequest ("save", reqData);
        if (respData.empty ()) {
            throw std::runtime_error (
              "Impossible to save requested features");
        }

        srr::SrrSaveResponse resp;

        cxxtools::SerializationInfo respSi;
        JSON::readFromString(respData.back(), respSi);

        respSi >>= resp;

        if(!resp.m_error.empty()) {
            std::cout << "Error: " << resp.m_error << std::endl;
        }

        os << respData.back() << std::endl;
    }
    catch (std::exception &e) {
        std::cerr << e.what () << std::endl;
    }
}

void opRestore(const std::string& passphrase, std::istream& is) {
    std::string reqJson;
    is >> reqJson;
    
    cxxtools::SerializationInfo siJson;
    JSON::readFromString(reqJson, siJson);

    srr::SrrRestoreRequest req;
    req.m_passphrase = passphrase;
    siJson.getMember("version") >>= req.m_version;
    siJson.getMember("checksum") >>= req.m_checksum;

    if(req.m_version == "1.0") {
        srr::SrrRestoreRequestDataV1 reqData;
        siJson.getMember("data") >>= reqData.m_data;
        req.m_data_ptr = std::shared_ptr<srr::SrrRestoreRequestData>(new srr::SrrRestoreRequestDataV1(reqData));
    } else if(req.m_version == "2.0") {
        srr::SrrRestoreRequestDataV2 reqData;
        siJson.getMember("data") >>= reqData.m_data;
        req.m_data_ptr = std::shared_ptr<srr::SrrRestoreRequestData>(new srr::SrrRestoreRequestDataV2(reqData));
    } else {
        std::cerr << "Invalid SRR version" << std::endl;
        return;
    }

    cxxtools::SerializationInfo reqSi;
    reqSi <<= req;

    try {
        dto::UserData reqData;
        reqData.push_back(JSON::writeToString(reqSi, false));

        // Send request
        dto::UserData respData = sendRequest ("restore", reqData);
        if (respData.empty ()) {
            throw std::runtime_error (
              "Impossible to save requested features");
        }

        srr::SrrRestoreResponse resp;

        cxxtools::SerializationInfo respSi;
        JSON::readFromString(respData.back(), respSi);

        respSi >>= resp;

        if(!resp.m_error.empty()) {
            std::cout << "Error: " << resp.m_error << std::endl;
        }
    }
    catch (std::exception &e) {
        std::cerr << e.what () << std::endl;
    }
}

void opReset() {
    std::cerr << "Srr daemon does not handle reset operation" << std::endl;
}
