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

/// \file   MID/Workflow/src/TrackerSpec.cxx
/// \brief  Data processor spec for MID tracker device
/// \author Gabriele G. Fronze <gfronze at cern.ch>
/// \date   9 July 2018

#include "MIDWorkflow/TrackerSpec.h"

#include <chrono>
#include "Framework/DataRefUtils.h"
#include "Framework/ConfigParamRegistry.h"
#include "Framework/ControlService.h"
#include "Framework/Logger.h"
#include "Framework/Output.h"
#include "Framework/Task.h"
#include "DataFormatsMID/Cluster3D.h"
#include "DataFormatsMID/Track.h"
#include "MIDTracking/Tracker.h"
#include "DetectorsBase/GeometryManager.h"
#include "CommonUtils/NameConf.h"

namespace of = o2::framework;

namespace o2
{
namespace mid
{
class TrackerDeviceDPL
{
 public:
  void init(o2::framework::InitContext& ic)
  {

    if (!gGeoManager) {
      o2::base::GeometryManager::loadGeometry();
    }

    mTracker = std::make_unique<Tracker>(createTransformationFromManager(gGeoManager));

    if (!mTracker->init(true)) {
      LOG(error) << "Initialization of MID tracker device failed";
    }

    auto stop = [this]() {
      LOG(info) << "Capacities: ROFRecords: " << mTracker->getTrackROFRecords().capacity() << "  tracks: " << mTracker->getTracks().capacity() << "  clusters: " << mTracker->getClusters().capacity();
      double scaleFactor = 1.e6 / mNROFs;
      LOG(info) << "Processing time / " << mNROFs << " ROFs: full: " << mTimer.count() * scaleFactor << " us  tracking: " << mTimerAlgo.count() * scaleFactor << " us";
    };
    ic.services().get<of::CallbackService>().set(of::CallbackService::Id::Stop, stop);
  }

  void run(o2::framework::ProcessingContext& pc)
  {
    auto tStart = std::chrono::high_resolution_clock::now();

    auto msg = pc.inputs().get("mid_clusters");
    gsl::span<const Cluster2D> clusters = of::DataRefUtils::as<const Cluster2D>(msg);

    auto msgROF = pc.inputs().get("mid_clusters_rof");
    gsl::span<const ROFRecord> inROFRecords = of::DataRefUtils::as<const ROFRecord>(msgROF);

    auto tAlgoStart = std::chrono::high_resolution_clock::now();
    mTracker->process(clusters, inROFRecords);
    mTimerAlgo += std::chrono::high_resolution_clock::now() - tAlgoStart;

    pc.outputs().snapshot(of::Output{"MID", "TRACKS", 0, of::Lifetime::Timeframe}, mTracker->getTracks());
    LOG(debug) << "Sent " << mTracker->getTracks().size() << " tracks.";
    pc.outputs().snapshot(of::Output{"MID", "TRACKCLUSTERS", 0, of::Lifetime::Timeframe}, mTracker->getClusters());
    LOG(debug) << "Sent " << mTracker->getClusters().size() << " track clusters.";

    pc.outputs().snapshot(of::Output{"MID", "TRACKROFS", 0, of::Lifetime::Timeframe}, mTracker->getTrackROFRecords());
    LOG(debug) << "Sent " << mTracker->getTrackROFRecords().size() << " ROFs.";
    pc.outputs().snapshot(of::Output{"MID", "TRCLUSROFS", 0, of::Lifetime::Timeframe}, mTracker->getClusterROFRecords());
    LOG(debug) << "Sent " << mTracker->getClusterROFRecords().size() << " ROFs.";

    mTimer += std::chrono::high_resolution_clock::now() - tStart;
    mNROFs += inROFRecords.size();
  }

 private:
  std::unique_ptr<Tracker> mTracker{nullptr};
  std::chrono::duration<double> mTimer{0};     ///< full timer
  std::chrono::duration<double> mTimerAlgo{0}; ///< algorithm timer
  unsigned int mNROFs{0};                      /// Total number of processed ROFs
};

framework::DataProcessorSpec getTrackerSpec()
{
  std::vector<of::InputSpec> inputSpecs{of::InputSpec{"mid_clusters", "MID", "CLUSTERS"}, of::InputSpec{"mid_clusters_rof", "MID", "CLUSTERSROF"}};

  std::vector<of::OutputSpec> outputSpecs{
    of::OutputSpec{"MID", "TRACKS"},
    of::OutputSpec{"MID", "TRACKCLUSTERS"},
    of::OutputSpec{"MID", "TRACKROFS"},
    of::OutputSpec{"MID", "TRCLUSROFS"}};

  return of::DataProcessorSpec{
    "MIDTracker",
    {inputSpecs},
    {outputSpecs},
    of::adaptFromTask<o2::mid::TrackerDeviceDPL>(),
    of::Options{}};
}
} // namespace mid
} // namespace o2
