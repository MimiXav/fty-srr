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
    struct SrrInvalidVersion : public std::exception
    {
        SrrInvalidVersion() {};
        SrrInvalidVersion(const std::string& err) : m_err(err) {};

        std::string m_err = "Invalid SRR version";

        const char * what () const throw ()
        {
            return m_err.c_str();
        }
    };

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

    struct SrrResetFailed : public std::exception
    {
        SrrResetFailed() {};
        SrrResetFailed(const std::string& err) : m_err(err) {};

        std::string m_err = "Reset failed";

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

    static void evalDataIntegrity(Group& group, const std::string& passphrase)
    {
        // sort features by priority
        std::sort(group.m_features.begin(), group.m_features.end(), [&] (SrrFeature l, SrrFeature r) {
            return getPriority(l.m_feature_name) > getPriority(r.m_feature_name);
        });

        // evaluate data integrity
        cxxtools::SerializationInfo tmpSi;
        tmpSi <<= group.m_features;
        const std::string data = serializeJson(tmpSi, false);

        group.m_data_integrity = fty::encrypt(data, passphrase);
    }

    static bool checkDataIntegrity(Group& group, const std::string& passphrase)
    {
        cxxtools::SerializationInfo tmpSi;
        tmpSi <<= group.m_features;
        const std::string data = serializeJson(tmpSi, false);

        std::string decryptedData = fty::decrypt(group.m_data_integrity, passphrase);

        return decryptedData == data;
    }

    static std::map<std::string, std::set<FeatureName>> groupFeaturesByAgent(const std::list<FeatureName>& features)
    {
        std::map<std::string, std::set<FeatureName>> map;

        for(const auto& feature : features)
        {
            try{
                const std::string& agentName = SrrFeatureMap.at(feature).m_agent;
                map[agentName].insert(feature);
            } catch (std::out_of_range&){
                log_warning("Feature %s not found", feature.c_str());
            }
        }

        return map;
    }

    dto::srr::SaveResponse SrrWorker::saveFeatures(const std::list<dto::srr::FeatureName>& features, const std::string& passphrase)
    {
        // group calls by destination agent
        std::map<std::string, std::set<FeatureName>> featureAgentMap = groupFeaturesByAgent(features);

        SaveResponse response;

        // build save query with all the features that call the same agent
        for(auto const& mapEntry: featureAgentMap)
        {
            const std::string& agentNameDest = mapEntry.first;
            const std::string& queueNameDest = agentToQueue[agentNameDest];
            
            const auto& featuresByAgent = mapEntry.second;

            log_debug("Request save of features %s to agent %s ", std::accumulate(featuresByAgent.begin(), featuresByAgent.end(), std::string(" ")).c_str(), agentNameDest.c_str());

            Query saveQuery = createSaveQuery({featuresByAgent}, passphrase);

            dto::UserData data;
            data << saveQuery;
            // Send message to agent
            messagebus::Message message = sendRequest(data, "save", queueNameDest, agentNameDest);
            log_debug("Save done by agent %s", agentNameDest.c_str());

            Response featureResponse;
            message.userData() >> featureResponse;
            // concatenate all the responses from each agent
            response += featureResponse.save();
        }

        return response;
    }

    dto::srr::RestoreResponse SrrWorker::restoreFeature(const dto::srr::FeatureName& featureName, const dto::srr::RestoreQuery& query)
    {
        const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
        const std::string queueNameDest = agentToQueue[agentNameDest];

        Query restoreQuery;
        *(restoreQuery.mutable_restore()) = query;
        log_debug("Restoring configuration of %s by agent %s ", featureName.c_str(), agentNameDest.c_str());

        // Send message
        dto::UserData data;
        data << restoreQuery;
        messagebus::Message message = sendRequest(data, "restore", queueNameDest, agentNameDest);

        log_debug("%s restored by: %s ", featureName.c_str(), agentNameDest.c_str());
        Response response;

        message.userData() >> response;

        // restore procedure failed -> rollback
        if(response.mutable_restore()->status().status() != Status::SUCCESS) {
            throw SrrRestoreFailed("Restore procedure failed for feature " + featureName);
        }

        return response.restore();
    }

    dto::srr::ResetResponse SrrWorker::resetFeature(const dto::srr::FeatureName& featureName)
    {
        const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
        const std::string queueNameDest = agentToQueue[agentNameDest];

        Query query;
        ResetQuery& resetQuery = *(query.mutable_reset());
        *(resetQuery.mutable_version()) = m_srrVersion;
        resetQuery.add_features(featureName);

        dto::UserData data;
        data << query;
        messagebus::Message message = sendRequest(data, "reset", queueNameDest, agentNameDest);
        Response response;
        message.userData() >> response;

        if(response.reset().map_features_status().at(featureName).status() != Status::SUCCESS) {
            throw SrrResetFailed("Reset procedure failed for feature " + featureName);
        }

        return response.reset();
    }

    bool SrrWorker::rollback(const dto::srr::SaveResponse& rollbackSaveResponse, const std::string& passphrase)
    {
        bool restart = false;

        std::map<std::string, FeatureAndStatus> rollbackMap(rollbackSaveResponse.map_features_data().begin(), rollbackSaveResponse.map_features_data().end());

        for(auto entry : rollbackMap) {
            const FeatureName& featureName = entry.first;
            const dto::srr::Feature& featureData= entry.second.feature();

            const std::string agentNameDest = SrrFeatureMap.at(featureName).m_agent;
            const std::string queueNameDest = agentToQueue[agentNameDest];

            // Reset before restore (do not stop on failure)
            try{
                resetFeature(featureName);
            }
            catch (SrrResetFailed& ex) {
                log_warning(ex.what());
            }

            // Build restore query
            RestoreQuery restoreQuery;

            *(restoreQuery.mutable_version()) = m_srrVersion;
            *(restoreQuery.mutable_checksum()) = fty::encrypt(passphrase, passphrase);
            *(restoreQuery.mutable_passpharse()) = passphrase;
            restoreQuery.mutable_map_features_data()->insert({featureName, featureData});

            // restore backup data
            log_debug("Rollback configuration of %s by agent %s ", featureName.c_str(), agentNameDest.c_str());
            try{
                restoreFeature(featureName, restoreQuery);
            }
            catch (SrrRestoreFailed& ex) {
                log_error("Feature %s is unrecoverable. May be in undefined state", featureName.c_str());
            }
            log_debug("%s rolled back by: %s ", featureName.c_str(), agentNameDest.c_str());
            restart = restart | SrrFeatureMap.at(featureName).m_restart;
        }

        return restart;

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

            // check that passphrase is compliant with requested format
            if (fty::checkPassphraseFormat(srrSaveReq.m_passphrase))
            {
                // evalutate checksum
                srrSaveResp.m_checksum = fty::encrypt(srrSaveReq.m_passphrase, srrSaveReq.m_passphrase);

                log_debug("Save IPM2 configuration processing");

                std::list<dto::srr::FeatureName> featuresToSave;
                
                for(const auto& groupId : srrSaveReq.m_group_list) {
                    try {
                        const auto& group = SrrGroupMap.at(groupId);
                        for(const auto& fp : group.m_fp) {
                            featuresToSave.push_back(fp.m_feature);
                        }
                    } catch (std::out_of_range&){
                        log_error("Group %s not found", groupId.c_str());
                    }
                }

                SaveResponse saveResp = saveFeatures(featuresToSave, srrSaveReq.m_passphrase);

                // convert ProtoBuf save response to UI DTO
                const auto& mapFeaturesData = saveResp.map_features_data();

                std::map<std::string, Group> savedGroups;

                for (const auto& fs : mapFeaturesData) {
                    SrrFeature f;
                    f.m_feature_name = fs.first;
                    f.m_feature_and_status = fs.second;

                    // save each feature into its group
                    const std::string groupId = getGroupFromFeature(f.m_feature_name);
                    if(groupId.empty()) {
                        log_error("Feature %s is not part of any group. Will not be included in the Save payload", f.m_feature_name.c_str());
                    } else {
                        savedGroups[groupId].m_features.push_back(f);
                    }
                }

                // update group info and evaluate data integrity
                for(auto groupElement : savedGroups) {
                    const auto& groupId = groupElement.first;
                    auto& group = groupElement.second;

                    group.m_group_id = groupId;
                    group.m_group_name = groupId;

                    // evaluate data integrity
                    evalDataIntegrity(group, srrSaveReq.m_passphrase);

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

        response.push_back(srrSaveResp.m_status);
        response.push_back(jsonResp);

        return response;
    }

    dto::UserData SrrWorker::requestRestore(const std::string& json, bool force)
    {
        bool restart = false;

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

            if(srrRestoreReq.m_version == "1.0") {
                const auto& features = srrRestoreReq.m_data_ptr->getSrrFeatures();

                for(const auto& feature : features) {
                    const auto& featureName = feature.m_feature_name;
                    const auto& dtoFeature = feature.m_feature_and_status.feature();
                    // prepare restore query
                    RestoreQuery query;
                    query.set_passpharse(srrRestoreReq.m_passphrase);
                    query.mutable_map_features_data()->insert({featureName, dtoFeature});

                    RestoreStatus restoreStatus;
                    restoreStatus.m_name = featureName;

                    try {
                        RestoreResponse resp = restoreFeature(featureName, query);
                        restoreStatus.m_status = statusToString(resp.status().status());
                        restoreStatus.m_error = resp.status().error();
                    }
                    catch(SrrRestoreFailed& ex) {
                        restoreStatus.m_status = statusToString(Status::FAILED);
                        restoreStatus.m_error = ex.what();
                    }

                    srrRestoreResp.m_status_list.push_back(restoreStatus);
                }

            } else if(srrRestoreReq.m_version == "2.0") {
                std::list<std::string> groupsIntegrityCheckFailed;  // stores groups for which integrity check failed

                std::shared_ptr<SrrRestoreRequestDataV2> dataPtr = std::dynamic_pointer_cast<SrrRestoreRequestDataV2>(srrRestoreReq.m_data_ptr);
                auto& groups = dataPtr->m_data;

                if(force) {
                    log_warning("Restoring with force option: data integrity check will be skipped");
                }

                for(auto& group : groups) {
                    // sort features by priority
                    std::sort(group.m_features.begin(), group.m_features.end(), [&] (SrrFeature l, SrrFeature r) {
                        return getPriority(l.m_feature_name) > getPriority(r.m_feature_name);
                    });

                    if(!force) {
                        // check data integrity
                        if(!checkDataIntegrity(group, srrRestoreReq.m_passphrase)) {
                            log_error("Integrity check failed for group %s", group.m_group_id.c_str());
                            groupsIntegrityCheckFailed.push_back(group.m_group_id);
                        }
                    }
                }

                if(!force && !groupsIntegrityCheckFailed.empty()) {
                    throw srr::SrrIntegrityCheckFailed(
                        "Data integrity check failed for groups:" + std::accumulate(groupsIntegrityCheckFailed.begin(),groupsIntegrityCheckFailed.end(), std::string(" ")));
                }

                RestoreResponse response;

                // restore each group
                for(const auto& group : groups) {
                    const auto& groupId = group.m_group_id;

                    if(SrrGroupMap.find(group.m_group_id) == SrrGroupMap.end()) {
                        log_error("Group %s does not exist, will not be restored", groupId.c_str());
                        continue;   
                    }

                    std::map<std::string, dto::srr::FeatureAndStatus> ftMap;
                    for(const auto& feature : group.m_features) {
                        ftMap[feature.m_feature_name] = feature.m_feature_and_status;
                    }

                    // store all restore queries related to the current group
                    std::map<FeatureName, RestoreQuery> restoreQueriesMap;

                    try {
                        // loop through all required features to create the restore queries
                        for (const auto& feature : SrrGroupMap.at(groupId).m_fp) {
                            const auto& featureName = feature.m_feature;
                            const auto& dtoFeature = ftMap.at(featureName).feature();

                            // prepare restore queries
                            RestoreQuery& request = restoreQueriesMap[featureName];
                            request.set_passpharse(srrRestoreReq.m_passphrase);
                            request.mutable_map_features_data()->insert({featureName, dtoFeature});
                        }
                    }   // if one feature is missing, set the error for the whole group and skip the group
                    catch (std::out_of_range& ex) {
                        RestoreStatus restoreStatus;
                        restoreStatus.m_name = groupId;
                        restoreStatus.m_status = statusToString(Status::FAILED);
                        restoreStatus.m_error = "Group " + groupId + " cannot be restored. Missing features";

                        srrRestoreResp.m_status_list.push_back(restoreStatus);

                        log_error(restoreStatus.m_error.c_str());

                        continue;
                    }

                    SaveResponse rollbackSaveResponse;

                    try{
                        // loop though all the features in the group
                        for(const auto& feature : group.m_features) {
                            const FeatureName& featureName = feature.m_feature_name;

                            log_debug("Processing feature %s", featureName.c_str());

                            // Save current status -> rollback in case of error
                            rollbackSaveResponse += saveFeatures({featureName}, srrRestoreReq.m_passphrase);

                            // Restore feature
                            response += restoreFeature(featureName, restoreQueriesMap[featureName]);

                            // update restart flag
                            restart = restart | SrrFeatureMap.at(featureName).m_restart;
                        }
                    }
                    catch (const std::exception& ex) {
                        // group restore failed -> rolling back
                        RestoreStatus restoreStatus;
                        restoreStatus.m_name = groupId;
                        restoreStatus.m_status = statusToString(Status::FAILED);
                        restoreStatus.m_error = ex.what();
                        srrRestoreResp.m_status_list.push_back(restoreStatus);

                        log_error(restoreStatus.m_error.c_str());

                        log_info("Starting group %s rollback", groupId.c_str());

                        restart = restart | rollback(rollbackSaveResponse, srrRestoreReq.m_passphrase);
                    }
                }

                srrRestoreResp.m_status = statusToString(Status::SUCCESS);

                std::map<std::string, FeatureStatus> rspMap(response.map_features_status().begin(), response.map_features_status().end());

                // create restore reply of all features that restored properly
                for(const auto& element : rspMap) {
                    RestoreStatus restoreStatus;

                    restoreStatus.m_name = element.first;
                    restoreStatus.m_status = statusToString(element.second.status());
                    restoreStatus.m_error = element.second.error();

                    srrRestoreResp.m_status_list.push_back(restoreStatus);
                }

            } else {
                throw SrrInvalidVersion();
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
