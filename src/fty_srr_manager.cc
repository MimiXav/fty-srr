/*  =========================================================================
    fty_srr_server - Fty srr server

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
    fty_srr_server - Fty srr server
@discuss
@end
 */

#include "srr_pb.h"
#include "fty_srr_classes.h"

using namespace std::placeholders;
using namespace dto::srr;

namespace srr
{
    /**
     * Constructor
     * @param parameters
     * @param streamPublisher
     */
    SrrManager::SrrManager(const std::map<std::string, std::string> & parameters)
    : m_parameters(parameters)
    {
        init();
    }
    
    /**
     * Class initialization 
     */
    void SrrManager::init()
    {
        try
        {
            // Message bus init
            m_msgBus = std::unique_ptr<messagebus::MessageBus>(messagebus::MlmMessageBus(m_parameters.at(ENDPOINT_KEY), m_parameters.at(AGENT_NAME_KEY)));
            m_msgBus->connect();
            
            // Worker creation.
            m_srrworker = std::unique_ptr<srr::SrrWorker>(new srr::SrrWorker(*m_msgBus, m_parameters));
            
            // Bind all processor handler.
            m_processor.listFeatureHandler = std::bind(&SrrWorker::getFeatureListManaged, m_srrworker.get(), _1);
            //m_processor.listFeatureHandler = std::bind(&SrrManager::getListFeatureHandler, this, _1);
            m_processor.saveHandler = std::bind(&SrrWorker::saveIpm2Configuration, m_srrworker.get(), _1);
            m_processor.restoreHandler = std::bind(&SrrWorker::restoreIpm2Configuration, m_srrworker.get(), _1);
            m_processor.resetHandler = std::bind(&SrrWorker::resetIpm2Configuration, m_srrworker.get(), _1);
            
            // Listen all incoming request
            //messagebus::Message fct = [&](messagebus::Message msg){this->handleRequest(msg);};
            auto fct = std::bind(&SrrManager::handleRequest, this, _1);
            m_msgBus->receive(m_parameters.at(SRR_QUEUE_NAME_KEY), fct);
        }        
        catch (messagebus::MessageBusException& ex)
        {
            log_error("Message bus error: %s", ex.what());
            throw SrrException("Failed to open connection with message bus!");
        } catch (...)
        {
            log_error("Unexpected error: unknown");
            throw SrrException("Unexpected error: unknown");
        }
    }

    /**
     * Handle all incoming request
     * @param sender
     * @param payloadea
     * @return 
     */
    void SrrManager::handleRequest(messagebus::Message msg)
    {
        log_debug("SRR handle request");
        try
        {
            dto::UserData data = msg.userData();
            // Get the query
            Query query;
            data >> query;
            // Process the query
            Response response = m_processor.processQuery(query);
            // Send response
            dto::UserData respData;
            respData << response;
            sendResponse(msg, respData);
        }        
        catch (std::exception& ex)
        {
            log_error(ex.what());
        }
    }

    /**
     * Send response on message bus
     * @param msg
     * @param userData
     */
    void SrrManager::sendResponse(const messagebus::Message& msg, const dto::UserData& userData)
    {
        try
        {
            messagebus::Message respMsg;
            respMsg.userData() = userData;
            respMsg.metaData().emplace(messagebus::Message::SUBJECT, msg.metaData().at(messagebus::Message::SUBJECT));
            respMsg.metaData().emplace(messagebus::Message::FROM, m_parameters.at(AGENT_NAME_KEY));
            respMsg.metaData().emplace(messagebus::Message::TO, msg.metaData().find(messagebus::Message::FROM)->second);
            respMsg.metaData().emplace(messagebus::Message::CORRELATION_ID, msg.metaData().find(messagebus::Message::CORRELATION_ID)->second);
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
}