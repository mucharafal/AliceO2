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

/// \file Clusterer.cxx
/// \brief Implementation of the HMPID cluster finder
#include <algorithm>
#include "FairLogger.h" // for LOG
#include "Framework/Logger.h"
#include "DataFormatsHMP/Cluster.h"
#include "HMPIDReconstruction/Clusterer.h"
#include "SimulationDataFormat/MCCompLabel.h"
#include "SimulationDataFormat/MCTruthContainer.h"
#include <TStopwatch.h>

using namespace o2::hmpid;

//__________________________________________________
void Clusterer::process(std::vector<o2::hmpid::Digit> const& digits, std::vector<o2::hmpid::Cluster>& clusters, MCLabelContainer const* digitMCTruth)
{
  TStopwatch timerProcess;
  timerProcess.Start();

  //  reader.init();
  //  int totNumDigits = 0;
  //
  //  while (reader.getNextStripData(mStripData)) {
  //    LOG(debug) << "HMPIDClusterer got Strip " << mStripData.stripID << " with Ndigits "
  //               << mStripData.digits.size();
  //    totNumDigits += mStripData.digits.size();
  //
  //    processStrip(clusters, digitMCTruth);
  //  }

  //  LOG(debug) << "We had " << totNumDigits << " digits in this event";
  timerProcess.Stop();
  printf("Timing:\n");
  printf("Clusterer::process:        ");
  timerProcess.Print();
}
