# Copyright CERN and copyright holders of ALICE O2. This software is distributed
# under the terms of the GNU General Public License v3 (GPL Version 3), copied
# verbatim in the file "COPYING".
#
# See http://alice-o2.web.cern.ch/license for full licensing information.
#
# In applying this license CERN does not waive the privileges and immunities
# granted to it by virtue of its status as an Intergovernmental Organization or
# submit itself to any jurisdiction.

o2_add_library(
  MIDFiltering
  SOURCES src/ChannelMasksHandler.cxx src/ChannelScalers.cxx src/FetToDead.cxx
          src/MaskMaker.cxx
  PUBLIC_LINK_LIBRARIES O2::DataFormatsMID O2::MIDBase O2::MIDRaw
                        Microsoft.GSL::GSL)

add_subdirectory(exe)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
