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

#ifndef FTY_SRR_WORKER_H_INCLUDED
#define FTY_SRR_WORKER_H_INCLUDED

#include <fty_common_messagebus.h>

namespace srr 
{
    struct FeaturesAssociation 
    {
            std::string agentName;
            std::string queueName;
            
            FeaturesAssociation() = default;
            FeaturesAssociation(const std::string agentName, const std::string queueName):
                agentName(agentName),
                queueName(queueName) {}
    };
    
    class SrrWorker
    {
        public:

          explicit SrrWorker(messagebus::MessageBus* msgBus, const std::map<std::string, std::string>& parameters);
          ~SrrWorker();
          
          void getFeatureListManaged(const messagebus::Message& msg, const std::string& subject);
          void saveIpm2Configuration(const messagebus::Message& msg, const dto::srr::SrrQueryDto& query);
          void restoreIpm2Configuration(const messagebus::Message& msg, const dto::srr::SrrQueryDto& query);

        private:
          std::map<std::string, std::string> m_parameters;
          std::map<std::string, FeaturesAssociation> m_featuresAssociation;
          
          std::map<const std::string, std::string> m_agentQueueAssociation;
          
          messagebus::MessageBus *m_msgBus;

          void init();
          void buildFeaturesAssociation();
          
          void saveFactorizationCall(const cxxtools::SerializationInfo& siFeatureList, std::map<const std::string, std::string>& association);
          void restoreFactorizationCall(cxxtools::SerializationInfo& siData, std::map<const std::string, cxxtools::SerializationInfo>& association);

          void sendResponse(const messagebus::Message& msg, const messagebus::UserData& userData, const std::string& subject);

          cxxtools::SerializationInfo buildIpm2ConfigurationStruct();
          void buildResponsePayload(const std::string& featureName, cxxtools::SerializationInfo& siOutput, cxxtools::SerializationInfo& siInput);
    };    
}

#endif
