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

    struct SrrRestoreFailed : public std::exception
    {
        SrrRestoreFailed() {};
        SrrRestoreFailed(const std::string& err) : m_err(err) {};

        std::string m_err = "Restore failed";

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
        SrrListResponse srrListResp;

        srrListResp.m_version = m_srrVersion;
        srrListResp.m_passphrase_description = TRANSLATE_ME("Passphrase must have %s characters", (fty::getPassphraseFormat()).c_str());
        srrListResp.m_passphrase_validation = fty::getPassphraseFormat();

        for (const auto& mapEntry : SrrGroupMap) {
            const std::string& groupId = mapEntry.first;
            const SrrGroupStruct& srrGroup = mapEntry.second;

            GroupInfo groupInfo;
            groupInfo.m_group_id = groupId;
            groupInfo.m_group_name = srrGroup.m_name;
            groupInfo.m_description = TRANSLATE_ME((std::string(SRR_PREFIX_TRANSLATE_KEY) + srrGroup.m_description).c_str());

            for (const auto& featureAndPriority : srrGroup.m_fp) {
                const std::string& featureId = featureAndPriority.m_feature;

                FeatureInfo featureInfo;

                featureInfo.m_name = featureId;
                featureInfo.m_description = SrrFeatureMap.at(featureId).m_description;

                groupInfo.m_features.push_back(featureInfo);
            }

            srrListResp.m_groups.push_back(groupInfo);
        }

        cxxtools::SerializationInfo si;
        si <<= srrListResp;

        dto::UserData response;
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
            SrrSaveRequest srrSaveReq;

            requestSi >>= srrSaveReq;

            // update checksum
            srrSaveResp.m_checksum = fty::encrypt(srrSaveReq.m_passphrase, srrSaveReq.m_passphrase);

            // check that passphrase is compliant with requested format
            if (fty::checkPassphraseFormat(srrSaveReq.m_passphrase))
            {
                log_debug("Save IPM2 configuration processing");
                // group calls by destination agent
                std::map<std::string, std::set<FeatureName>> featureAgentMap;
                for(const auto& groupName: srrSaveReq.m_group_list)
                {
                    try {
                        const auto& group = SrrGroupMap.at(groupName);

                        for (const auto& feature : group.m_fp) {
                            const std::string& featureName = feature.m_feature;
                            const std::string& agentName = SrrFeatureMap.at(featureName).m_agent;

                            featureAgentMap[agentName].insert(featureName);
                        }
                    } catch (std::out_of_range&){
                        log_warning("group %s not found", groupName.c_str());
                    }
                }

                SaveResponse saveResp;

                // build save query with all the features that call the same agent
                for(auto const& mapEntry: featureAgentMap)
                {
                    std::string agentNameDest = mapEntry.first;
                    std::string queueNameDest = agentToQueue[agentNameDest];

                    log_debug("Request save to agent %s ", agentNameDest.c_str());

                    Query saveQuery = createSaveQuery({mapEntry.second}, srrSaveReq.m_passphrase);

                    dto::UserData reqData;
                    reqData << saveQuery;
                    // Send message to agent
                    messagebus::Message agentReply = sendRequest(reqData, "save", queueNameDest, agentNameDest);
                    log_debug("Save done by agent %s", agentNameDest.c_str());

                    Response partialResp;
                    agentReply.userData() >> partialResp;
                    // concatenate all the responses from each agent
                    saveResp += partialResp.save();
                }

                // convert ProtoBuf save response to UI DTO
                const google::protobuf::Map<std::string, FeatureAndStatus> & mapFeaturesData = saveResp.map_features_data();

                std::map<std::string, Group> savedGroups;

                for (const auto& fs : mapFeaturesData) {
                    SrrFeature f;
                    f.m_feature_name = fs.first;
                    f.m_feature_and_status = fs.second;

                    // save each feature into its group
                    const std::string groupName = getGroupFromFeature(f.m_feature_name);
                    if(groupName.empty()) {
                        log_error("Feature %s is not part of any group. Will not be included in the Save payload", f.m_feature_name.c_str());
                    } else {
                        savedGroups[groupName].m_features.push_back(f);
                    }
                }

                // update group info and evaluate data integrity
                for(auto groupElement : savedGroups) {
                    const auto& groupName = groupElement.first;
                    auto& group = groupElement.second;

                    group.m_group_id = groupName;
                    group.m_group_name = groupName;

                    // evaluate data integrity
                    cxxtools::SerializationInfo tmpSi;
                    tmpSi <<= group.m_features;
                    const std::string featureData = serializeJson(tmpSi, false);
                    group.m_data_integrity = fty::encrypt(featureData, srrSaveReq.m_passphrase);

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

        std::list<std::string> groupIntegrityCheckFailed;

        SrrRestoreResponse srrRestoreResp;

        srrRestoreResp.m_status = statusToString(Status::FAILED);

        try
        {
            cxxtools::SerializationInfo requestSi = dto::srr::deserializeJson(json);
            SrrRestoreRequest srrRestoreReq;

            requestSi >>= srrRestoreReq;

            std::string passphrase = fty::decrypt(srrRestoreReq.m_checksum, srrRestoreReq.m_passphrase);

            if(passphrase.compare(srrRestoreReq.m_passphrase) != 0) {
                throw std::runtime_error("Invalid passphrase");
            }

            std::map<FeatureName, RestoreQuery> restoreQueriesMap;
            std::list<FeatureName> featuresToRestore;
            std::set<std::string> requiredGroups;

            // get all features to restore
            for(const auto& srrFeature : srrRestoreReq.m_data_ptr->getSrrFeatures()) {
                const auto& featureName = srrFeature.m_feature_name;
                const auto& feature = srrFeature.m_feature_and_status.feature();
                featuresToRestore.push_back(featureName);

                std::string group = getGroupFromFeature(featureName);
                if(!group.empty()) {
                    requiredGroups.insert(group);

                    // prepare restore query
                    RestoreQuery& request = restoreQueriesMap[featureName];
                    request.set_passpharse(srrRestoreReq.m_passphrase);
                    request.mutable_map_features_data()->insert({featureName, feature});
                } else {
                    log_warning("Feature %s does not belong to any group - will not be restored", featureName.c_str());
                }
            }

            // data integrity check
            if(srrRestoreReq.m_version == "2.0")
            {
                if(!force) {
                    std::shared_ptr<SrrRestoreRequestDataV2> dataPtr = std::dynamic_pointer_cast<SrrRestoreRequestDataV2>(srrRestoreReq.m_data_ptr);

                    const auto& data = dataPtr->m_data;

                    for(const auto& group : data) {
                        cxxtools::SerializationInfo tmpSi;
                        tmpSi <<= group.m_features;
                        const std::string jsonData = serializeJson(tmpSi, false);

                        std::string decryptedData = fty::decrypt(group.m_data_integrity, srrRestoreReq.m_passphrase);

                        if(decryptedData != jsonData) {
                            groupIntegrityCheckFailed.push_back(group.m_group_id);
                            log_error("Integrity check failed for group %s", group.m_group_id.c_str());
                        }
                    }
                } else {
                    log_warning("Restore with force option: skipping data integrity check");
                }
            } else {
                log_warning("Version 1.0 does not support data integrity check - skipping check");
            }

            if(!force && !groupIntegrityCheckFailed.empty()) {
                throw srr::SrrIntegrityCheckFailed(
                    "Data integrity check failed for groups:" + std::accumulate(groupIntegrityCheckFailed.begin(),groupIntegrityCheckFailed.end(), std::string(" ")));
            }

            RestoreResponse response;

            // check if all features in the group are available
            for(const auto& group : requiredGroups) {
                auto featuresInGroup(SrrGroupMap.at(group));
                log_debug("Restoring group %s", group.c_str());

                std::vector<std::string> missingFeatures;
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

                    SaveResponse rollbackSaveReponse;

                    try{

                        for(const auto& fp : featuresInGroup.m_fp) {
                            const FeatureName& featureName = fp.m_feature;

                            log_debug("Processing feature %s", featureName.c_str());

                            const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
                            const std::string queueNameDest = agentToQueue[agentNameDest];

                            // Save current status -> rollback in case of error
                            ////////////////////////////////////////////////////////
                            Query rollbackSaveQuery = createSaveQuery({featureName}, srrRestoreReq.m_passphrase);
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
                            *(restoreQuery.mutable_restore()) = restoreQueriesMap[featureName];
                            log_debug("Restoring configuration of %s by agent %s ", featureName.c_str(), agentNameDest.c_str());
                            // Send message
                            dto::UserData reqData;
                            reqData << restoreQuery;
                            messagebus::Message resp = sendRequest(reqData, "restore", queueNameDest, agentNameDest);
                            log_debug("%s restored by: %s ", featureName.c_str(), agentNameDest.c_str());
                            Response partialResp;

                            resp.userData() >> partialResp;
                            if(partialResp.mutable_restore()->status().status() != Status::SUCCESS) {
                                throw SrrRestoreFailed("Restore procedure failed for feature " + featureName + ". Rolling back changes");
                            }

                            response += partialResp.restore();

                            restart = restart | SrrFeatureMap.at(featureName).m_restart;
                        }
                    }
                    catch (const std::exception& ex) {
                        RestoreStatus restoreStatus;
                        restoreStatus.m_name = group;
                        restoreStatus.m_status = statusToString(Status::FAILED);
                        restoreStatus.m_error = ex.what();
                        srrRestoreResp.m_status_list.push_back(restoreStatus);

                        log_error(restoreStatus.m_error.c_str());

                        log_info("Starting group %s rollback", group.c_str());

                        // rollback restored features in the group
                        std::map<std::string, FeatureAndStatus> rollbackMap(rollbackSaveReponse.map_features_data().begin(), rollbackSaveReponse.map_features_data().end());

                        for(auto entry : rollbackMap) {
                            const FeatureName& featureName = entry.first;
                            const dto::srr::Feature& featureData= entry.second.feature();

                            const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
                            const std::string queueNameDest = agentToQueue[agentNameDest];

                            // Reset before restore
                            Query resetQueryGeneric;
                            ResetQuery& resetQuery = *(resetQueryGeneric.mutable_reset());
                            *(resetQuery.mutable_version()) = m_srrVersion;
                            resetQuery.add_features(featureName);

                            dto::UserData resetData;
                            resetData << resetQueryGeneric;
                            messagebus::Message resetMsg = sendRequest(resetData, "reset", queueNameDest, agentNameDest);
                            Response resetResp;
                            resetMsg.userData() >> resetResp;

                            const auto& resetMap = resetResp.mutable_reset()->mutable_map_features_status();
                            if(resetMap->at(featureName).status() != Status::SUCCESS) {
                                log_warning("Reset of feature %s failed", featureName.c_str());
                            }

                            // Build restore query
                            RestoreQuery restoreQuery;

                            *(restoreQuery.mutable_version()) = m_srrVersion;
                            *(restoreQuery.mutable_checksum()) = srrRestoreReq.m_checksum;
                            *(restoreQuery.mutable_passpharse()) = srrRestoreReq.m_passphrase;
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

                } else {
                    RestoreStatus restoreStatus;
                    restoreStatus.m_name = group;
                    restoreStatus.m_status = statusToString(Status::FAILED);
                    restoreStatus.m_error = "Group " + group + " cannot be restored. Missing features: " + std::accumulate(missingFeatures.begin(), missingFeatures.end(), std::string(" "));

                    srrRestoreResp.m_status_list.push_back(restoreStatus);

                    log_error(restoreStatus.m_error.c_str());
                }
            }

            srrRestoreResp.m_status = statusToString(Status::SUCCESS);

            std::map<std::string, FeatureStatus> rspMap(response.map_features_status().begin(), response.map_features_status().end());

            for(const auto& element : rspMap) {
                RestoreStatus restoreStatus;

                restoreStatus.m_name = element.first;
                restoreStatus.m_status = statusToString(element.second.status());
                restoreStatus.m_error = element.second.error();

                srrRestoreResp.m_status_list.push_back(restoreStatus);
            }
        }
        catch (const SrrIntegrityCheckFailed& e)
        {
            std::string errorMsg = e.what();
            srrRestoreResp.m_status = statusToString(Status::UNKNOWN);
            srrRestoreResp.m_error = errorMsg;

            log_error(errorMsg.c_str());
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
        response.push_back(srrRestoreResp.m_status);
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
