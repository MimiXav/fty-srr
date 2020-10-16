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

#include "fty_srr_groups.h"

#include <algorithm>

namespace srr
{

std::string getGroupFromFeature(const std::string& featureName)
{
    std::string groupId;
    for(const auto& group : SrrGroupMap) {
        const auto& fp = group.second.m_fp;
        auto found = std::find_if(fp.begin(), fp.end(), [&](SrrFeaturePriorityStruct x) {
            return (featureName == x.m_feature);
        });

        if(found != fp.end()) {
            groupId = group.first;
            break;
        }
    }

    return groupId;
}

}
