/*  =========================================================================
    fty_srr_server - Fty srr server

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
            
            // Create the worker.
            m_srrworker = std::unique_ptr<srr::SrrWorker>(new srr::SrrWorker(*m_msgBus, m_parameters));
            
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
        log_debug("SRR handleRequest");
        try
        {
            dto::UserData data = msg.userData();
            Query query;
            //data >> query;
            //log_debug("Query action: %s", actionToString(query.action).c_str());
            
            /*Response*/ SrrQueryProcessor::processQuery(query);
            
//            switch (query.action)
//            {
//                case (Action::GET_FEATURE_LIST):
//                {
//                    m_srrworker->getFeatureListManaged(msg);
//                    break;
//                }
//                case (Action::SAVE):
//                {
//                    m_srrworker->saveIpm2Configuration(msg, query);
//                    break;
//                }
//                case (Action::RESTORE):
//                {
//                    m_srrworker->restoreIpm2Configuration(msg, query);
//                    break;
//                }
//                default:
//                    log_error("Wrong command '%s'", actionToString(query.action).c_str());
//            }
        }        
        catch (std::exception& ex)
        {
            log_error(ex.what());
        }
    }
}