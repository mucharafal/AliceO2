// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <string>

#include <FairMQDevice.h>
#include "Headers/DataHeader.h"
#include "Framework/Logger.h"
#include "Framework/ProcessingContext.h"
#include "Framework/RawDeviceService.h"
#include "Framework/DataRefUtils.h"
#include "Framework/InputRecord.h"
#include "Framework/ServiceRegistry.h"

#include "TPCWorkflow/ProcessingHelpers.h"

using namespace o2::framework;
using namespace o2::tpc;

// taken from CTFWriterSpec, TODO: should this be put to some more general location?
uint64_t processing_helpers::getRunNumber(ProcessingContext& pc)
{
  const std::string NAStr = "NA";

  uint64_t run = 0;
  const auto dh = DataRefUtils::getHeader<o2::header::DataHeader*>(pc.inputs().getFirstValid(true));
  if (dh->runNumber != 0) {
    run = dh->runNumber;
  }
  // check runNumber with FMQ property, if set, override DH number
  {
    auto runNStr = pc.services().get<RawDeviceService>().device()->fConfig->GetProperty<std::string>("runNumber", NAStr);
    if (runNStr != NAStr) {
      size_t nc = 0;
      auto runNProp = std::stol(runNStr, &nc);
      if (nc != runNStr.size()) {
        LOGP(error, "Property runNumber={} is provided but is not a number, ignoring", runNStr);
      } else {
        run = runNProp;
      }
    }
  }

  return run;
}
