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
    class SrrWorker
    {
        public:
            
            struct config {
                std::string agentName;
                std::string featureDescription;;
            };
            
            explicit SrrWorker(messagebus::MessageBus& msgBus, const std::map<std::string, std::string>& parameters);
            ~SrrWorker() = default;
          
            dto::srr::ListFeatureResponse getFeatureListManaged(const dto::srr::ListFeatureQuery& query);
            dto::srr::SaveResponse saveIpm2Configuration(const dto::srr::SaveQuery& query);
            dto::srr::RestoreResponse restoreIpm2Configuration(const dto::srr::RestoreQuery& query);
            dto::srr::ResetResponse resetIpm2Configuration(const dto::srr::ResetQuery& query);

        private:
            messagebus::MessageBus& m_msgBus;
            std::map<std::string, std::string> m_parameters;
            std::string m_srrVersion;
            std::map<const std::string, config> m_featuresToAgent;
            std::map<const std::string, std::string> m_agentToQueue;
   
            void init();
            void buildMapAssociation();
            bool isVerstionCompatible(const std::string& version);

            std::map<std::string, std::set<dto::srr::FeatureName>> factorizationSaveCall(const dto::srr::SaveQuery query);
            std::map<std::string, dto::srr::RestoreQuery> factorizationRestoreCall(const dto::srr::RestoreQuery query);

            messagebus::Message sendRequest(const dto::UserData& userData, const std::string& action, const std::string& queueNameDest, const std::string& agentNameDest);
    };    
}

#endif
