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

#include "TRDWorkflowIO/TRDCalibratedTrackletWriterSpec.h"
#include "DataFormatsTRD/CalibratedTracklet.h"
#include <SimulationDataFormat/MCTruthContainer.h>
#include <SimulationDataFormat/MCCompLabel.h>

#include "DPLUtils/MakeRootTreeWriterSpec.h"

using namespace o2::framework;

namespace o2
{
namespace trd
{

template <typename T>
using BranchDefinition = framework::MakeRootTreeWriterSpec::BranchDefinition<T>;

o2::framework::DataProcessorSpec getTRDCalibratedTrackletWriterSpec(bool useMC)
{
  using MakeRootTreeWriterSpec = framework::MakeRootTreeWriterSpec;

  return MakeRootTreeWriterSpec("calibrated-tracklet-writer",
                                "trdcalibratedtracklets.root",
                                "ctracklets",
                                BranchDefinition<std::vector<CalibratedTracklet>>{InputSpec{"ctracklets", "TRD", "CTRACKLETS"}, "CTracklets"},
                                BranchDefinition<o2::dataformats::MCTruthContainer<o2::MCCompLabel>>{InputSpec{"trklabels", "TRD", "TRKLABELS"}, "TRKLabels", (useMC ? 1 : 0), "TRKLABELS"},
                                BranchDefinition<std::vector<char>>{InputSpec{"trigrecmask", "TRD", "TRIGRECMASK"}, "TrigRecMask"})();
};

} // end namespace trd
} // end namespace o2
