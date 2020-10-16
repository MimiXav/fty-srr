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
#include "fty_srr_groups.h"
#include "fty_srr_exception.h"
#include "fty_srr_worker.h"

#include "dto/request.h"
#include "dto/response.h"

#include <fty_common.h>
#include <fty_lib_certificate_library.h>

#include <chrono>
#include <cstdlib>
#include <numeric>
#include <vector>
#include <thread>
#include <unistd.h>

#define SRR_RESTART_DELAY_SEC 5

using namespace dto::srr;

namespace srr
{
    struct SrrIntegrityCheckFailed : public std::exception
    {
        SrrIntegrityCheckFailed() {};
        SrrIntegrityCheckFailed(const std::string& err) : m_err(err) {};

        std::string m_err = "Integrity Check Failed";

        const char * what () const throw ()
        {
            return m_err.c_str();
        }
    };

    static void restartBiosService(const unsigned restartDelay)
    {
        for(unsigned i = restartDelay; i > 0; i--) {
            log_info("Rebooting in %d seconds...", i);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        log_info("Reboot");
        // write out buffer to disk
        sync();
        // int ret = std::system("sudo /sbin/reboot");
        // if (ret) {
        //     log_error("failed to reboot");
        // }
    }

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

        SrrListResponse listResp;

        listResp.m_version = m_srrVersion;
        listResp.m_passphrase_description = TRANSLATE_ME("Passphrase must have %s characters", (fty::getPassphraseFormat()).c_str());
        listResp.m_passphrase_validation = fty::getPassphraseFormat();

        for (const auto& grp : SrrGroupMap) {
            const std::string& groupId = grp.first;
            const SrrGroupStruct& srrGroup = grp.second;

            GroupInfo g;
            g.m_group_id = groupId;
            g.m_group_name = srrGroup.m_name;
            g.m_description = TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + srrGroup.m_description).c_str());

            for (const auto& ft : srrGroup.m_fp) {
                const std::string& featureId = ft.m_feature;

                FeatureInfo fi;

                fi.m_name = featureId;
                fi.m_description = SrrFeatureMap.at(featureId).m_description;

                g.m_features.push_back(fi);
            }

            listResp.m_groups.push_back(g);
        }

        cxxtools::SerializationInfo si;
        si <<= listResp;

        response.push_back(dto::srr::serializeJson(si));

        log_debug("%s", response.front().c_str());

        return response;
    }

    dto::UserData SrrWorker::requestSave(const std::string& json)
    {
        SrrSaveResponse srrSaveResp;

        srrSaveResp.m_version = m_srrVersion;     
        srrSaveResp.m_status = statusToString(Status::FAILED);

        try
        {
            cxxtools::SerializationInfo requestSi = dto::srr::deserializeJson(json);
            SrrSaveRequest req;

            requestSi >>= req;

            // update checksum
            srrSaveResp.m_checksum = fty::encrypt(req.m_passphrase, req.m_passphrase);

            // check passphrase is compliant with requested format
            if (fty::checkPassphraseFormat(req.m_passphrase))
            {
                log_debug("Save IPM2 configuration processing");
                // group calls by destination agent
                std::map<std::string, std::set<FeatureName>> featureAgentMap;
                for(const auto& groupName: req.m_group_list)
                {
                    try {
                        for (const auto& feature : SrrGroupMap.at(groupName).m_fp) {
                            const std::string& featureName = feature.m_feature;
                            const std::string& agentName = SrrFeatureMap.at(featureName).m_agent;

                            featureAgentMap[agentName].insert(featureName);
                        }
                    } catch (std::out_of_range&){
                        log_warning("group %s not found", groupName.c_str());
                    }
                }

                SaveResponse saveResp;

                for(auto const& mapEntry: featureAgentMap)
                {
                    // Get queue name from agent name
                    std::string agentNameDest = mapEntry.first;
                    std::string queueNameDest = agentToQueue[agentNameDest];

                    log_debug("Request save to agent %s ", agentNameDest.c_str());
                    // Build query with all the features that call the same agent
                    Query saveQuery = createSaveQuery({mapEntry.second}, req.m_passphrase);

                    dto::UserData reqData;
                    reqData << saveQuery;
                    // Send message to agent
                    messagebus::Message resp = sendRequest(reqData, "save", queueNameDest, agentNameDest);
                    log_debug("Save done by agent %s", agentNameDest.c_str());

                    Response partialResp;
                    resp.userData() >> partialResp;
                    saveResp += partialResp.save();
                }

                // convert ProtoBuf save response to UI DTO
                google::protobuf::Map<std::string, FeatureAndStatus> & mapFeaturesData = *(saveResp.mutable_map_features_data());

                std::map<std::string, Group> savedGroups;

                for (const auto& fs : mapFeaturesData) {
                    SrrFeature f;
                    f.m_feature_name = fs.first;
                    f.m_feature_and_status = fs.second;

                    savedGroups[getGroupFromFeature(f.m_feature_name)].m_features.push_back(f);
                }

                // update group info and evaluate data integrity
                for(auto groupElement : savedGroups) {
                    const auto& groupName = groupElement.first;
                    auto& group = groupElement.second;

                    group.m_group_id = groupName;
                    group.m_group_name = groupName;
                    //TODO update
                    group.m_data_integrity = fty::encrypt(groupName, req.m_passphrase);

                    srrSaveResp.m_data.push_back(group);
                }
                srrSaveResp.m_status = statusToString(Status::SUCCESS);
            }
            else
            {
                const std::string error = TRANSLATE_ME("Passphrase must have %s characters", (fty::getPassphraseFormat()).c_str());
                srrSaveResp.m_error = error;
                log_error(error.c_str());
            }
        }
        catch (const std::exception& e)
        {
            const std::string error = TRANSLATE_ME("Exception on save Ipm2 configuration: (%s)", e.what());
            srrSaveResp.m_error = error;
            log_error(error.c_str());
        }

        dto::UserData response;

        cxxtools::SerializationInfo responseSi;
        responseSi <<= srrSaveResp;

        std::string jsonResp = serializeJson(responseSi);
        response.push_back(jsonResp);

        return response;
    }

    dto::UserData SrrWorker::requestRestore(const std::string& json, bool force)
    {
        bool restart = false;
        bool rollback = false;

        SrrRestoreResponse srrRestoreResp;

        srrRestoreResp.m_status = statusToString(Status::FAILED);

        try
        {
            cxxtools::SerializationInfo requestSi = dto::srr::deserializeJson(json);
            SrrRestoreRequest req;

            requestSi >>= req;

            std::string decryptedData = fty::decrypt(req.m_checksum, req.m_passphrase);

            if(decryptedData.compare(req.m_passphrase) != 0) {
                throw std::runtime_error("Invalid passphrase");
            }

            std::map<FeatureName, RestoreQuery> rq;
            std::list<FeatureName> featuresToRestore;
            std::set<std::string> requiredGroups;

            // data integrity check
            if(!force) {
                log_info("Implement data integrity check");
                if(false) {
                    throw srr::SrrIntegrityCheckFailed("Data integrity check failed");
                }
            } else {
                log_warning("Restore with force option: skipping data integrity check");
            }

            for(const auto& srrFeature : req.m_data_ptr->getSrrFeatures()) {
                const auto& featureName = srrFeature.m_feature_name;
                const auto& feature = srrFeature.m_feature_and_status.feature();
                featuresToRestore.push_back(featureName);

                std::string group = getGroupFromFeature(featureName);
                if(!group.empty()) {
                    requiredGroups.insert(group);

                    RestoreQuery& request = rq[featureName];
                    request.set_passpharse(req.m_passphrase);
                    request.mutable_map_features_data()->insert({featureName, feature});
                } else {
                    log_warning("Feature %s does not belong to any group - skip", featureName.c_str());
                }
            }

            RestoreResponse response;
            SaveResponse rollbackSaveReponse;

            for(const auto& group : requiredGroups) {
                auto featuresInGroup(SrrGroupMap.at(group));
                log_debug("Restoring group %s", group.c_str());

                std::vector<std::string> missingFeatures;

                // check if all features in the group are available
                for(const auto& fp : featuresInGroup.m_fp) {
                    const auto& featureName = fp.m_feature;

                    auto found = std::find(featuresToRestore.begin(), featuresToRestore.end(), featureName);
                    if(found == featuresToRestore.end()) {
                        log_error("Missing feature %s", featureName.c_str());
                        missingFeatures.push_back(featureName);
                    }
                }

                // restore features
                if(missingFeatures.empty()) {
                    // sort features by priority
                    std::sort(featuresInGroup.m_fp.begin(), featuresInGroup.m_fp.end(), [&] (SrrFeaturePriorityStruct l, SrrFeaturePriorityStruct r) {
                        return l.m_priority < r.m_priority;
                    });

                    for(const auto& fp : featuresInGroup.m_fp) {
                        const FeatureName& featureName = fp.m_feature;

                        log_debug("Processing feature %s", featureName.c_str());

                        const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
                        const std::string queueNameDest = agentToQueue[agentNameDest];

                        // Save current status -> rollback in case of error
                        ////////////////////////////////////////////////////////
                        Query rollbackSaveQuery = createSaveQuery({featureName}, req.m_passphrase);
                        dto::UserData rollbackSaveData;
                        rollbackSaveData << rollbackSaveQuery;
                        // Send message to agent
                        messagebus::Message rollbackResp = sendRequest(rollbackSaveData, "save", queueNameDest, agentNameDest);
                        log_debug("Back up of feature %s", featureName.c_str());

                        Response partialRollbackResp;
                        rollbackResp.userData() >> partialRollbackResp;
                        rollbackSaveReponse += partialRollbackResp.save();
                        ////////////////////////////////////////////////////////

                        // Build restore query
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

                        restart = restart | SrrFeatureMap.at(featureName).m_restart;
                    }
                } else {
                    RestoreStatus restoreStatus;
                    restoreStatus.m_name = group;
                    restoreStatus.m_status = statusToString(Status::FAILED);
                    restoreStatus.m_error = "Group " + group + " cannot be restored. Missing features: " + std::accumulate(missingFeatures.begin(), missingFeatures.end(), std::string(" "));

                    srrRestoreResp.m_status_list.push_back(restoreStatus);

                    log_error(restoreStatus.m_error.c_str());
                }
            }

            if(rollback) {
                std::map<std::string, FeatureAndStatus> rollbackMap(rollbackSaveReponse.map_features_data().begin(), rollbackSaveReponse.map_features_data().end());

                for(auto entry : rollbackMap) {
                    const FeatureName& featureName = entry.first;
                    const dto::srr::Feature& featureData= entry.second.feature();

                    const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
                    const std::string queueNameDest = agentToQueue[agentNameDest];

                    // Build restore query
                    RestoreQuery restoreQuery;

                    *(restoreQuery.mutable_version()) = m_srrVersion;
                    *(restoreQuery.mutable_checksum()) = req.m_checksum;
                    *(restoreQuery.mutable_passpharse()) = req.m_passphrase;
                    restoreQuery.mutable_map_features_data()->insert({featureName, featureData});

                    log_debug("Rollback configuration of %s by agent %s ", featureName.c_str(), agentNameDest.c_str());
                    // Send message
                    dto::UserData reqData;

                    Query rollbackQuery;
                    *(rollbackQuery.mutable_restore()) = restoreQuery;
                    reqData << rollbackQuery;
                    messagebus::Message resp = sendRequest(reqData, "restore", queueNameDest, agentNameDest);
                    log_debug("%s rolled back by: %s ", featureName.c_str(), agentNameDest.c_str());
                    Response partialResp;

                    resp.userData() >> partialResp;
                    response += partialResp.restore();

                    restart = restart | SrrFeatureMap.at(featureName).m_restart;
                }
            }

            srrRestoreResp.m_status = statusToString(Status::SUCCESS);

            std::map<std::string, FeatureStatus> rspMap(response.map_features_status().begin(), response.map_features_status().end());

            for(const auto& element : rspMap) {
                log_debug("Restore response from %s", element.first.c_str());

                RestoreStatus restoreStatus;

                restoreStatus.m_name = element.first;
                restoreStatus.m_status = statusToString(element.second.status());
                restoreStatus.m_error = element.second.error();

                srrRestoreResp.m_status_list.push_back(restoreStatus);
            }
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = e.what();
            srrRestoreResp.m_error = errorMsg;

            log_error(errorMsg.c_str());
        }

        cxxtools::SerializationInfo responseSi;
        responseSi <<= srrRestoreResp;

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
