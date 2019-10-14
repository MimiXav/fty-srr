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

#ifndef FTY_SRR_SERVER_H_INCLUDED
#define FTY_SRR_SERVER_H_INCLUDED

#include <functional>

#include <fty_common_messagebus.h>
#include "fty_common_client.h"
#include "fty_common_sync_server.h"
#include "fty_srr_worker.h"

/**
 * \brief Agent config server actor
 */

namespace srr 
{
    class SrrManager 
    {
        public:

            explicit SrrManager(const std::map<std::string, std::string> & parameters);
            ~SrrManager();
            
        private:

            std::map<std::string, std::string> m_parameters;
            messagebus::MessageBus *m_msgBus;
            srr::SrrWorker* m_srrworker;

            void init();
            void handleRequest(messagebus::Message msg);
    };
    
} // namespace srr

#endif