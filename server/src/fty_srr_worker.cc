/*  =========================================================================
    fty_srr_worker - Fty srr worker

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
    fty_srr_worker - Fty srr worker
@discuss
@end
 */

// clang-format off

#include "fty-srr.h"
#include "fty_srr_exception.h"
#include "fty_srr_worker.h"

#include <fty_common.h>
#include <fty_lib_certificate_library.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <thread>
#include <unistd.h>

#define SRR_RESTART_DELAY_SEC 5

using namespace dto::srr;

namespace srr
{
    static void restartBiosService(const unsigned restartDelay)
    {
        for(unsigned i = restartDelay; i > 0; i--) {
            log_info("Rebooting in %d seconds...", i);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        log_info("Reboot");
        // write out buffer to disk
        sync();
        int ret = std::system("sudo /sbin/reboot");
        if (ret) {
            log_error("failed to reboot");
        }
    }

    static std::map<const std::string, std::vector<std::pair<std::string, unsigned int>>> groupFeatures = {
        {
            G_ASSETS, {
                { F_ASSET_AGENT    , 1 }/* ,
                { F_VIRTUAL_ASSETS , 2 } */
            }
        },
        {
            G_CONFIG, {
                { F_AUTOMATION_SETTINGS       , 1 },
                { F_DISCOVERY                 , 2 },
                { F_MASS_MANAGEMENT           , 2 },
                { F_MONITORING_FEATURE_NAME   , 3 },
                { F_NETWORK                   , 4 },
                { F_NOTIFICATION_FEATURE_NAME , 5 },
                { F_USER_SESSION_FEATURE_NAME , 6 }
            }
        },
        {
            G_SECW, {
                { F_SECURITY_WALLET , 1}
            }
        }
    };

    static std::string getGroupFromFeature(const std::string& featureName)
    {
        std::string groupName;
        for(const auto& group : groupFeatures) {
            const auto& features = group.second;
            auto found = std::find_if(features.begin(), features.end(), [&](std::pair<std::string, unsigned int> x) {
                return (featureName == x.first);
            });

            if(found != features.end()) {
                groupName = group.first;
                break;
            }
        }

        return groupName;
    }

    static std::map<const std::string, const std::string> featureDescription = {
        { F_ALERT_AGENT               , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_ALERT_AGENT).c_str()) },
        { F_ASSET_AGENT               , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_ASSET_AGENT).c_str()) },
        { F_AUTOMATION_SETTINGS       , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_AUTOMATION_SETTINGS).c_str()) },
        { F_AUTOMATIONS               , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_AUTOMATIONS).c_str()) },
        { F_DISCOVERY                 , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_DISCOVERY).c_str()) },
        { F_MASS_MANAGEMENT           , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_MASS_MANAGEMENT).c_str()) },
        { F_MONITORING_FEATURE_NAME   , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_MONITORING_FEATURE_NAME).c_str()) },
        { F_NOTIFICATION_FEATURE_NAME , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_NOTIFICATION_FEATURE_NAME).c_str()) },
        { F_NETWORK                   , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_NETWORK).c_str()) },
        { F_SECURITY_WALLET           , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_SECURITY_WALLET).c_str()) },
        { F_USER_SESSION_FEATURE_NAME , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_USER_SESSION_FEATURE_NAME).c_str()) },
        { F_VIRTUAL_ASSETS            , TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + F_VIRTUAL_ASSETS).c_str()) }
    };

    static std::map<const std::string, const std::string> featureToAgent = {
        { F_ALERT_AGENT               , ALERT_AGENT_NAME },
        { F_ASSET_AGENT               , ASSET_AGENT_NAME },
        { F_AUTOMATION_SETTINGS       , CONFIG_AGENT_NAME },
        { F_AUTOMATIONS               , EMC4J_AGENT_NAME },
        { F_DISCOVERY                 , CONFIG_AGENT_NAME },
        { F_MASS_MANAGEMENT           , CONFIG_AGENT_NAME },
        { F_MONITORING_FEATURE_NAME   , CONFIG_AGENT_NAME },
        { F_NETWORK                   , CONFIG_AGENT_NAME },
        { F_NOTIFICATION_FEATURE_NAME , CONFIG_AGENT_NAME },
        { F_SECURITY_WALLET           , SECU_WALLET_AGENT_NAME },
        { F_USER_SESSION_FEATURE_NAME , CONFIG_AGENT_NAME },
        { F_VIRTUAL_ASSETS            , EMC4J_AGENT_NAME }
    };

    static std::map<const std::string, const std::string> agentToQueue = {
        { ALERT_AGENT_NAME      , ALERT_AGENT_MSG_QUEUE_NAME },
        { ASSET_AGENT_NAME      , ASSET_AGENT_MSG_QUEUE_NAME },
        { CONFIG_AGENT_NAME     , CONFIG_MSG_QUEUE_NAME },
        { EMC4J_AGENT_NAME      , EMC4J_MSG_QUEUE_NAME },
        { SECU_WALLET_AGENT_NAME, SECU_WALLET_MSG_QUEUE_NAME }
    };

    static std::map<const std::string, bool> restartAfterRestore = {
        { F_ALERT_AGENT               , true },
        { F_ASSET_AGENT               , true },
        { F_AUTOMATION_SETTINGS       , true },
        { F_AUTOMATIONS               , true },
        { F_DISCOVERY                 , true },
        { F_MASS_MANAGEMENT           , true },
        { F_MONITORING_FEATURE_NAME   , true },
        { F_NETWORK                   , true },
        { F_NOTIFICATION_FEATURE_NAME , true },
        { F_SECURITY_WALLET           , true },
        { F_USER_SESSION_FEATURE_NAME , true },
        { F_VIRTUAL_ASSETS            , true }
    };

    /**
     * Constructor
     * @param msgBus
     * @param parameters
     */
    SrrWorker::SrrWorker(messagebus::MessageBus& msgBus, const std::map<std::string, std::string>& parameters, const std::set<std::string> supportedVersions) :
        m_msgBus(msgBus), m_parameters(parameters), m_supportedVersions(supportedVersions)
    {
        init();
    }
    
    /**
     * Init srr worker
     */
    void SrrWorker::init()
    {
        try
        {
            // Srr version
            m_srrVersion = m_parameters.at(SRR_VERSION_KEY);
        }        
        catch (messagebus::MessageBusException& ex)
        {
            throw SrrException(ex.what());
        } catch (...)
        {
            throw SrrException("Unexpected error: unknown");
        }
    }

    // UI interface
    dto::UserData SrrWorker::getGroupList()
    {
        dto::UserData response;

        cxxtools::SerializationInfo si;

        si.addMember("version") <<= m_srrVersion;
        si.addMember("passphrase_description") <<= TRANSLATE_ME("Passphrase must have %s characters", (fty::getPassphraseFormat()).c_str());
        si.addMember("passphrase_validation") <<= fty::getPassphraseFormat();

        auto& groupsSi = si.addMember("");

        for (const auto& grp : groupFeatures) {
            auto& groupSi = groupsSi.addMember("");

            const std::string& groupName = grp.first;
            log_debug("- %s", groupName.c_str());

            groupSi.addMember("group_id") <<= "0";
            groupSi.addMember("group_name") <<= groupName;
            groupSi.addMember("description") <<= TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + groupName).c_str());

            auto& featuresSi = groupSi.addMember("");

            for (const auto& ft : grp.second) {
                const std::string& featureName = ft.first;
                const unsigned int featurePriority = ft.second;
                log_debug("\t- %s (%d)", featureName.c_str(), featurePriority);

                auto& featureSi = featuresSi.addMember("");
                featureSi.addMember("name") <<= featureName;
                featureSi.addMember("description") <<= featureName;

                featureSi.setCategory(cxxtools::SerializationInfo::Object);
            }

            featuresSi.setCategory(cxxtools::SerializationInfo::Array);
            featuresSi.setName("features");

            groupSi.setCategory(cxxtools::SerializationInfo::Object);
        }

        groupsSi.setCategory(cxxtools::SerializationInfo::Array);
        groupsSi.setName("groups");

        response.push_back(dto::srr::serializeJson(si));

        log_debug("%s", response.front().c_str());

        return response;
    }

    dto::UserData SrrWorker::requestSave(const std::string& json)
    {
        // create json response
        cxxtools::SerializationInfo responseSi;           
        
        responseSi.addMember("version") <<= m_srrVersion;
        std::string responseStatus = statusToString(Status::FAILED);
        std::string responseError  = "";

        try
        {
            cxxtools::SerializationInfo si;
            si = dto::srr::deserializeJson(json);

            std::string passphrase;
            si.getMember(PASS_PHRASE) >>= passphrase;

            responseSi.addMember("checksum") <<= fty::encrypt(passphrase, passphrase);

            const cxxtools::SerializationInfo & groupSi = si.getMember(GROUP_LIST);
            std::list<std::string> groupList;
            groupSi >>= groupList;

            bool checkPassphraseFormat = fty::checkPassphraseFormat(passphrase);

            if (checkPassphraseFormat)
            {
                log_debug("Save IPM2 configuration processing");
                // Try to factorize all call.
                std::map<std::string, std::set<FeatureName>> assoc;
                for(const auto& groupName: groupList)
                {
                    log_debug("requested group: %s", groupName.c_str());

                    try {
                        for (const auto& feature : groupFeatures.at(groupName)) {
                            const std::string& featureName = feature.first;
                            const std::string& agentName = featureToAgent[featureName];

                            log_debug("- saving feature %s", featureName.c_str());

                            assoc[agentName].insert(featureName);
                        }
                    } catch (std::out_of_range&){
                        log_warning("group %s not found", groupName.c_str());
                    }
                }

                SaveResponse saveResp;

                for(auto const& agent: assoc)
                {
                    // Get queue name from agent name
                    std::string agentNameDest = agent.first;
                    std::string queueNameDest = agentToQueue[agentNameDest];

                    log_debug("Saving configuration by: %s ", agentNameDest.c_str());
                    // Build query
                    Query saveQuery = createSaveQuery({agent.second}, passphrase);
                    // Send message
                    dto::UserData reqData;
                    reqData << saveQuery;
                    messagebus::Message resp = sendRequest(reqData, "save", queueNameDest, agentNameDest);
                    log_debug("Save done by %s", agentNameDest.c_str());

                    Response partialResp;
                    resp.userData() >> partialResp;
                    saveResp += partialResp.save();
                }

                auto& responseData = responseSi.addMember("");

                google::protobuf::Map<std::string, FeatureAndStatus> & mapFeaturesData = *(saveResp.mutable_map_features_data());

                for (const auto& fs : mapFeaturesData) {
                    const std::string& featureName = fs.first;
                    const std::string& featureStatus = statusToString(fs.second.status().status());
                    const std::string& featureError = fs.second.status().error();
                    Feature f = fs.second.feature();

                    auto& featureEntry = responseData.addMember("");
                    auto& featureSi = featureEntry.addMember(featureName);

                    featureSi.addMember("version") <<= m_srrVersion;
                    featureSi.addMember("status") <<= featureStatus;
                    featureSi.addMember("error") <<= featureError;
                    featureSi.addMember("data") <<= f;

                    featureEntry.setCategory(cxxtools::SerializationInfo::Object);
                    featureSi.setCategory(cxxtools::SerializationInfo::Object);
                }

                responseData.setName("data");
                responseData.setCategory(cxxtools::SerializationInfo::Array);

                responseStatus = statusToString(Status::SUCCESS);
            }
            else
            {
                const std::string error = TRANSLATE_ME("Passphrase must have %s characters", (fty::getPassphraseFormat()).c_str());
                responseError = error;
                log_error(error.c_str());
            }
        }
        catch (const std::exception& e)
        {
            const std::string error = TRANSLATE_ME("Exception on save Ipm2 configuration: (%s)", e.what());
            responseError = error;
            log_error(error.c_str());
        }

        responseSi.addMember("status") <<= responseStatus;
        responseSi.addMember("error") <<= responseError;

        dto::UserData response;
        std::string jsonResp = serializeJson(responseSi);
        response.push_back(jsonResp);

        return response;
    }

    dto::UserData SrrWorker::requestRestore(const std::string& json)
    {
        bool restart = false;

        // create json response
        cxxtools::SerializationInfo responseSi;           

        std::string responseStatus = statusToString(Status::FAILED);
        std::string responseError = "";

        std::string passphrase;
        std::string version;
        std::string checksum;

        try
        {
            cxxtools::SerializationInfo si;
            si = dto::srr::deserializeJson(json);
            si.getMember("passphrase") >>= passphrase;
            si.getMember("version") >>= version;
            si.getMember(CHECKSUM) >>= checksum;

            std::string decryptedData = fty::decrypt(checksum, passphrase);

            if(decryptedData.compare(passphrase) != 0) {
                throw std::runtime_error("Invalid passphrase");
            }

            std::map<FeatureName, RestoreQuery> rq;
            std::list<FeatureName> featuresToRestore;
            std::set<std::string> requiredGroups;

            const auto& dataSi = si.getMember("data");

            // parse different data version
            if(version == "1.0") {
                for(const auto& wrapSi : dataSi) {
                    auto& featureSi = wrapSi.getMember(0);

                    std::string featureName = featureSi.name();
                    Feature f;
                    featureSi >>= f;
                    featuresToRestore.push_back(featureName);
                    requiredGroups.insert(getGroupFromFeature(featureName));

                    RestoreQuery& request = rq[featureName];
                    request.set_passpharse(passphrase);
                    request.mutable_map_features_data()->insert({featureName, f});
                }
            } else if(version == "2.0") {
                for(const auto& entrySi : dataSi) {
                    std::string dataIntegrity;
                    entrySi.getMember("data_integrity") >>= dataIntegrity;
                    std::string groupId;
                    entrySi.getMember("group_id") >>= groupId;
                    std::string groupName;
                    entrySi.getMember("group_name") >>= groupName;

                    const auto& featuresSi = entrySi.getMember("features");

                    for(const auto& featureSi : featuresSi) {
                        std::string featureName = featureSi.name();
                        Feature f;
                        featureSi >>= f;
                        featuresToRestore.push_back(featureName);
                        requiredGroups.insert(getGroupFromFeature(featureName));

                        RestoreQuery& request = rq[featureName];
                        request.set_passpharse(passphrase);
                        request.mutable_map_features_data()->insert({featureName, f});
                    }
                }
            } else {
                throw std::runtime_error("Version " + version + " is not supported");
            }

            RestoreResponse response;

            // check that all features in a group are preset
            for(const auto& group : requiredGroups) {
                auto featuresInGroup(groupFeatures[group]);
                log_debug("Restoring group %s", group.c_str());

                std::vector<std::string> missingFeatures;

                // check if all features in the group are available
                for(const auto& feature : featuresInGroup) {
                    const auto& featureName = feature.first;

                    log_debug("Checking if feature %s is available", featureName.c_str());

                    auto found = std::find(featuresToRestore.begin(), featuresToRestore.end(), featureName);
                    if(found == featuresToRestore.end()) {
                        log_error("Group %s cannot be restored: missing feature %s", group.c_str(), featureName.c_str());
                        missingFeatures.push_back(featureName);
                    }
                }

                // sort features by priority
                std::sort(featuresInGroup.begin(), featuresInGroup.end(), [&] (std::pair<std::string, unsigned int> l, std::pair<std::string, unsigned int> r) {
                    return l.second < r.second;
                });

                // restore features
                for(const auto& feature : featuresInGroup) {
                    const FeatureName& featureName = feature.first;

                    log_debug("Processing feature %s", featureName.c_str());

                    if(missingFeatures.empty()) {
                        const std::string agentNameDest = featureToAgent[featureName];
                        const std::string queueNameDest = agentToQueue[agentNameDest];
                        // Build query
                        Query restoreQuery;
                        *(restoreQuery.mutable_restore()) = rq[featureName];
                        log_debug("Restoring configuration of %s by agent %s ", featureName.c_str(), agentNameDest.c_str());
                        // Send message
                        dto::UserData reqData;
                        reqData << restoreQuery;
                        messagebus::Message resp = sendRequest(reqData, "restore", queueNameDest, agentNameDest);
                        log_debug("%s restored by: %s ", featureName.c_str(), agentNameDest.c_str());
                        Response partialResp;
                        
                        resp.userData() >> partialResp;
                        response += partialResp.restore();

                        restart = restart | restartAfterRestore[featureName];
                    } else {
                        FeatureStatus ftStatus;
                        ftStatus.set_status(Status::FAILED);
                        ftStatus.set_error("Missing feature in group " + group);
                        response += (createRestoreResponse(ftStatus)).restore();
                    }
                }
            }

            responseStatus = statusToString(Status::SUCCESS);

            auto& statusListSi = responseSi.addMember("status_list");

            std::map<std::string, FeatureStatus> rspMap(response.map_features_status().begin(), response.map_features_status().end());

            for(const auto& element : rspMap) {
                auto& elementSi = statusListSi.addMember("");

                elementSi.addMember("name") <<= element.first;
                elementSi.addMember("status") <<= statusToString(element.second.status());
                elementSi.addMember("error") <<= element.second.error();

                elementSi.setCategory(cxxtools::SerializationInfo::Object);              
            }

            statusListSi.setCategory(cxxtools::SerializationInfo::Array);
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = e.what();
            responseError = errorMsg;

            log_error(errorMsg.c_str());
        }

        responseSi.addMember("status") <<= responseStatus;
        responseSi.addMember("error") <<= responseError;

        dto::UserData response;
        std::string jsonResp = serializeJson(responseSi);
        response.push_back(jsonResp);

        if(restart) {
            std::thread restartThread(restartBiosService, SRR_RESTART_DELAY_SEC);
            restartThread.detach();
        }

        return response;
    }

    dto::UserData SrrWorker::requestReset(const std::string& /* json */)
    {
        throw SrrException("Not implemented yet!");
    }
    
    /**
     * Send a response on the message bus.
     * @param msg
     * @param payload
     * @param subject
     */
    messagebus::Message SrrWorker::sendRequest(const dto::UserData& userData, const std::string& action, const std::string& queueNameDest, const std::string& agentNameDest)
    {
        messagebus::Message resp;
        try
        {
            int timeout = std::stoi(m_parameters.at(REQUEST_TIMEOUT_KEY)) / 1000;
            messagebus::Message req;
            req.userData() = userData;
            req.metaData().emplace(messagebus::Message::SUBJECT, action);
            req.metaData().emplace(messagebus::Message::FROM, m_parameters.at(AGENT_NAME_KEY));
            req.metaData().emplace(messagebus::Message::TO, agentNameDest);
            req.metaData().emplace(messagebus::Message::CORRELATION_ID, messagebus::generateUuid());
            resp = m_msgBus.request(queueNameDest, req, timeout);
        }
        catch (messagebus::MessageBusException& ex)
        {
            throw SrrException(ex.what());
        } catch (...)
        {
            throw SrrException("Unknown error on send response to the message bus");
        }
        return resp;
    }
    
    bool SrrWorker::isVerstionCompatible(const std::string& version)
    {
        return m_supportedVersions.find(version) != m_supportedVersions.end();
    }
    
} // namespace srr
