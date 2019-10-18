/*  =========================================================================
    fty_srr_worker - Fty srr worker

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

/*
@header
    fty_srr_worker - Fty srr worker
@discuss
@end
 */

#include "fty_srr_classes.h"

namespace srr
{
    // Timeout for Request/Reply in s
    static const int TIME_OUT = 4;
    
    /**
     * Constructor
     * @param msgBus
     * @param parameters
     */
    SrrWorker::SrrWorker(messagebus::MessageBus* msgBus, const std::map<std::string, std::string>& parameters)
    {
        m_msgBus = msgBus;
        m_parameters = parameters;
        init();
    }

    /**
     * Destructor
     */
    SrrWorker::~SrrWorker()
    {
    }
    
    /**
     * Init srr worker
     */
    void SrrWorker::init()
    {
        try
        {
            // Build map associations.
            buildMapAssociation();
        }        
        catch (messagebus::MessageBusException& ex)
        {
            throw SrrException(ex.what());
        } catch (...)
        {
            throw SrrException("Unexpected error: unknown");
        }
    }
    
    /**
     * Set all associations
     */
    void SrrWorker::buildMapAssociation()
    {
        // Feature -> Agent (fty-config)
        m_featuresToAgent [MONITORING_FEATURE_NAME] = CONFIG_AGENT_NAME;
        m_featuresToAgent [NOTIFICATION_FEATURE_NAME] = CONFIG_AGENT_NAME;
        m_featuresToAgent [AUTOMATION_SETTINGS] = CONFIG_AGENT_NAME;
        m_featuresToAgent [USER_SESSION_FEATURE_NAME] = CONFIG_AGENT_NAME;
        m_featuresToAgent [DISCOVERY] = CONFIG_AGENT_NAME;
        m_featuresToAgent [GENERAL_CONFIG] = CONFIG_AGENT_NAME;
        m_featuresToAgent [NETWORK] = CONFIG_AGENT_NAME;
        // Feature -> Agent (etn-malamute-translator EMC4J)
        m_featuresToAgent [AUTOMATIONS] = EMC4J_AGENT_NAME;
        m_featuresToAgent [VIRTUAL_ASSETS] = EMC4J_AGENT_NAME;
        // Agent -> Queue
        m_agentToQueue [CONFIG_AGENT_NAME] = CONFIG_MSG_QUEUE_NAME;
        m_agentToQueue [EMC4J_AGENT_NAME] = EMC4J_MSG_QUEUE_NAME;
    }
    
    /**
     * Get feature list managed
     * @param msg
     * @param subject
     */
    void SrrWorker::getFeatureListManaged(const messagebus::Message& msg, const std::string& subject)
    {
        dto::srr::SrrFeaturesListDto featuresListdto;
        for (const auto& feature : m_featuresToAgent)
        {
            featuresListdto.featuresList.push_back(feature.first);
        }
        // Send Response
        dto::UserData userData;
        userData << featuresListdto;
        sendResponse(msg, userData, subject);
    }
    
    /**
     * Save an Ipm2 configuration
     * @param msg
     * @param query
     */
    void SrrWorker::saveIpm2Configuration(const messagebus::Message& msg, const dto::srr::SrrQueryDto& query)
    {
        std::string saveResp;
        try
        {
            log_debug("Save IPM2 configuration processing");
            // Global serialization info.
            cxxtools::SerializationInfo ipm2ConfSi = buildIpm2ConfigurationStruct();
            
            // version check
            std::string version = m_parameters.at(SRR_VERSION_KEY);
            log_debug("Payload %s", query.data.c_str());
            
            if (query.data.size() > 0)
            {
                cxxtools::SerializationInfo si;
                JSON::readFromString(query.data, si);            
                
                // Get all feature association to factorize request
                std::map<const std::string, std::list<std::string>> agentAssoc;
                factorizationSaveCall(si.getMember(FEATURE_LIST_NAME), agentAssoc);
                
                for(auto const& agent: agentAssoc)
                {
                    // Get queue name from agent name
                    std::string agentNameDest = agent.first;
                    std::string queueNameDest = m_agentToQueue.at(agentNameDest);
                    //std::string features = agent.second;
                    
                    log_debug("Send request at '%s', to queue '%s', from '%s'", agentNameDest.c_str(), queueNameDest.c_str(), 
                            m_parameters.at(AGENT_NAME_KEY).c_str());
                    // Build query
                    dto::config::ConfigQueryDto configQuery;
                    configQuery.action = SAVE_ACTION;
                    configQuery.features = agent.second;
                    // Build message
                    messagebus::Message req;
                    req.userData() << configQuery;
                    req.metaData().emplace(messagebus::Message::SUBJECT, SAVE_ACTION);
                    req.metaData().emplace(messagebus::Message::FROM, m_parameters.at(AGENT_NAME_KEY));
                    req.metaData().emplace(messagebus::Message::TO, agentNameDest);
                    req.metaData().emplace(messagebus::Message::COORELATION_ID, messagebus::generateUuid());
                    // Send request
                    messagebus::Message resp = m_msgBus->request(queueNameDest, req, TIME_OUT);

                    log_debug("Settings retrieved");
                    // Response serialization 
                    dto::config::ConfigResponseDto configResponse(/*features*/"", STATUS_FAILED);
                    if (!resp.userData().empty())
                    {
                        resp.userData() >> configResponse;
                    }
                    // Get data member
                    cxxtools::SerializationInfo& si = *(ipm2ConfSi.findMember(DATA_MEMBER));
                    cxxtools::SerializationInfo siConfigResp;
                    // Get the serializationInfo from data response
                    JSON::readFromString (configResponse.data, siConfigResp);
                    // Iterate on array result
                    cxxtools::SerializationInfo::Iterator it;
                    for (it = siConfigResp.begin(); it != siConfigResp.end(); ++it)
                    {
                        si.addMember("") <<= (cxxtools::SerializationInfo)*it;
                    }
                }
                saveResp = JSON::writeToString(ipm2ConfSi, false);
            }
            else 
            {
                throw SrrException("Query data is empty");
            }
        }
        catch (messagebus::MessageBusException& ex)
        {
            log_error(ex.what());
        }
        catch (...)
        {
            log_error("Unknown error on save Ipm2 configuration");
        }
        // Send Response
        dto::UserData userData;
        userData.push_back(saveResp);
        sendResponse(msg, userData, query.action);
    }
    
    /**
     * Restore an Ipm2 Configuration
     * @param msg
     * @param query
     */
    void SrrWorker::restoreIpm2Configuration(const messagebus::Message& msg, const dto::srr::SrrQueryDto& query)
    {
        dto::srr::SrrRestoreDtoList respList;
        respList.status = STATUS_UNKNOWN;
        try
        {
            log_debug("Data to set %s", query.data.c_str());
            // Get the si from the request
            cxxtools::SerializationInfo si;
            JSON::readFromString(query.data, si);
            cxxtools::SerializationInfo siData = si.getMember(DATA_MEMBER);
            // Test if the data is well formated
            if (siData.category () != cxxtools::SerializationInfo::Array ) 
            {
                throw std::invalid_argument(DATA_MEMBER " should be an array");
            }
            // Factorization agent's call.
            std::map<const std::string, cxxtools::SerializationInfo> agentAssoc;
            factorizationRestoreCall(siData, agentAssoc);
            
            for(auto const& agent: agentAssoc)
            {
                // Restore serialization
                cxxtools::SerializationInfo restoreSi = agent.second;
                // Get queue name from agent name
                std::string agentNameDest = agent.first;
                std::string queueNameDest = m_agentToQueue.at(agentNameDest);
                // Build query
                dto::config::ConfigQueryDto configQuery(RESTORE_ACTION);
                configQuery.
                configQuery.data = "{" + JSON::writeToString(restoreSi, false) + "}";
                log_debug("Configuration to set %s: by: %s ", configQuery.data.c_str(), agentNameDest.c_str());
                //Send message
                messagebus::Message req;
                req.userData() << configQuery;
                req.metaData().emplace(messagebus::Message::SUBJECT, RESTORE_ACTION);
                req.metaData().emplace(messagebus::Message::FROM, m_parameters.at(AGENT_NAME_KEY));
                req.metaData().emplace(messagebus::Message::TO, agentNameDest);
                req.metaData().emplace(messagebus::Message::COORELATION_ID, messagebus::generateUuid());
                messagebus::Message resp = m_msgBus->request(queueNameDest, req, TIME_OUT);

                // Serialize response
                dto::srr::SrrRestoreDtoList respDto(STATUS_FAILED);
                if (!resp.userData().empty())
                {
                    resp.userData() >> respDto;

                    if ((respDto.status.compare(STATUS_FAILED) == 0 && respList.status.compare(STATUS_SUCCESS) == 0 )||
                        (respDto.status.compare(STATUS_SUCCESS) == 0 && respList.status.compare(STATUS_FAILED) == 0))
                    {
                        respList.status = STATUS_PARTIAL_SUCCESS;
                    }
                    else 
                    {
                        respList.status = respDto.status;
                    }
                }
                respList.responseList.insert(respList.responseList.end(), respDto.responseList.begin(), respDto.responseList.end());
            }
        } 
        catch (std::exception& ex)
        {
            throw SrrException(ex.what());
        }
        catch (...)
        {
            throw SrrException("Unknown error on restore Ipm2 configuration");
        }
        // Send Response
        dto::UserData userData;
        userData << respList;
        sendResponse(msg, userData, query.action);
    }

    /**
     * Save factorization by agent name.
     * @param siFeatureList
     * @param association
     */
    void SrrWorker::factorizationSaveCall(const cxxtools::SerializationInfo& siFeatureList, std::map<const std::string, std::list<std::string>>& association)
    {   
        for (const auto &feature : siFeatureList)
        {
            std::string featureName = "";
            feature.getMember(FEATURE_NAME).getValue(featureName);

            std::string agentName = m_featuresToAgent[featureName];
            if (association.count(agentName) == 0)
            {
                std::list<std::string> features;
                features.push_back(featureName);
                association[agentName] = features;
            }
            else
            {
                association[agentName].push_back(featureName);
            }
        }
    }
    
    /**
     * Restore factorization by agent name.
     * @param siFeatureList
     * @param association
     */
    void SrrWorker::factorizationRestoreCall(cxxtools::SerializationInfo& siData, std::map<const std::string, cxxtools::SerializationInfo>& association)
    {  
        cxxtools::SerializationInfo::Iterator it;
        for (it = siData.begin(); it != siData.end(); ++it)
        {
            cxxtools::SerializationInfo siTemp = (cxxtools::SerializationInfo)*it;
            for (const auto &si : siTemp)
            {
                std::string featureName = si.name();
                std::string agentName = m_featuresToAgent[featureName];
                if (association.count(agentName) == 0)
                {
                    // Create object
                    cxxtools::SerializationInfo siFeature;
                    siFeature.addMember("") <<= si;
                    // First one, create array
                    cxxtools::SerializationInfo siData;
                    siData.setCategory(cxxtools::SerializationInfo::Category::Array);
                    siData.addMember(featureName) <<= siFeature;
                    siData.setName(DATA_MEMBER);
                    association[agentName] = siData;
                }
                else
                {
                    // Create object
                    cxxtools::SerializationInfo siFeature;
                    siFeature.addMember("") <<= si;
                    association[agentName].addMember(featureName) <<= siFeature;
                }
            }
        }
        
    }
    
    /**
     * Send a response on the message bus.
     * @param msg
     * @param payload
     * @param subject
     */
    void SrrWorker::sendResponse(const messagebus::Message& msg, const dto::UserData& userData, const std::string& subject)
    {
        try
        {
            messagebus::Message respMsg;
            respMsg.userData() = userData;
            respMsg.metaData().emplace(messagebus::Message::SUBJECT, subject);
            respMsg.metaData().emplace(messagebus::Message::FROM, m_parameters.at(AGENT_NAME_KEY));
            respMsg.metaData().emplace(messagebus::Message::TO, msg.metaData().find(messagebus::Message::FROM)->second);
            respMsg.metaData().emplace(messagebus::Message::COORELATION_ID, msg.metaData().find(messagebus::Message::COORELATION_ID)->second);
            m_msgBus->sendReply(msg.metaData().find(messagebus::Message::REPLY_TO)->second, respMsg);
        }
        catch (messagebus::MessageBusException& ex)
        {
            throw SrrException(ex.what());
        } catch (...)
        {
            throw SrrException("Unknown error on send response to the message bus");
        }
    }
    
    /**
     * Utilitary to build an Ipm2 configuration si
     * @return 
     */
    cxxtools::SerializationInfo SrrWorker::buildIpm2ConfigurationStruct()
    {
        cxxtools::SerializationInfo si;
        si.addMember(SRR_VERSION_KEY) <<= ACTIVE_VERSION;
        // Data
        cxxtools::SerializationInfo& siData = si.addMember(DATA_MEMBER);
        siData.setCategory(cxxtools::SerializationInfo::Category::Array);
        return si;
    }
    
    /**
     *  Utilitary to build a response payload.
     * @return 
     */
    void SrrWorker::buildResponsePayload(const std::string& featureName, cxxtools::SerializationInfo& siOutput, cxxtools::SerializationInfo& siInput)
    {
        // Data
        cxxtools::SerializationInfo& si = *(siOutput.findMember(DATA_MEMBER));
        si.addMember(featureName) = siInput;
    }
    
} // namespace srr
