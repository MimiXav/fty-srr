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

#ifndef FTY_SRR_MANAGER_H_INCLUDED
#define FTY_SRR_MANAGER_H_INCLUDED

#include "fty_srr_worker.h"

/**
 * \brief Agent srr server
 */

// clang-format off
namespace srr 
{
    class SrrManager 
    {
        public:
            explicit SrrManager(const std::map<std::string, std::string> & parameters);
            ~SrrManager() = default;
            
            dto::srr::ListFeatureResponse getListFeatureHandler(const dto::srr::ListFeatureQuery& q);
            
        private:
            std::map<std::string, std::string> m_parameters;
            std::unique_ptr<messagebus::MessageBus> m_msgBus;
            std::unique_ptr<srr::SrrWorker> m_srrworker;
            
            dto::srr::SrrQueryProcessor m_processor;

            void init();
            void handleRequest(messagebus::Message msg);
            void sendResponse(const messagebus::Message& msg, const dto::UserData& userData);

            void msgHandler(const messagebus::Message msg);
    };
    
} // namespace srr

#endif
