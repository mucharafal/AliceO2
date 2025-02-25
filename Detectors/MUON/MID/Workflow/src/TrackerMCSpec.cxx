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

/// \file   MID/Workflow/src/TrackerMCSpec.cxx
/// \brief  Data processor spec for MID MC tracker device
/// \author Diego Stocco <Diego.Stocco at cern.ch>
/// \date   27 September 2019

#include "MIDWorkflow/TrackerMCSpec.h"

#include "Framework/ConfigParamRegistry.h"
#include "Framework/DataRefUtils.h"
#include "Framework/Logger.h"
#include "Framework/Output.h"
#include "Framework/Task.h"
#include "DataFormatsMID/Cluster3D.h"
#include "DataFormatsMID/ROFRecord.h"
#include "DataFormatsMID/Track.h"
#include "MIDSimulation/Geometry.h"
#include "MIDTracking/Tracker.h"
#include "DetectorsBase/GeometryManager.h"
#include "DataFormatsMID/MCClusterLabel.h"
#include "MIDSimulation/TrackLabeler.h"
#include "CommonUtils/NameConf.h"

namespace of = o2::framework;

namespace o2
{
namespace mid
{
class TrackerMCDeviceDPL
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
  }

  void run(o2::framework::ProcessingContext& pc)
  {
    auto msg = pc.inputs().get("mid_clusters");
    gsl::span<const Cluster2D> clusters = of::DataRefUtils::as<const Cluster2D>(msg);

    auto msgROF = pc.inputs().get("mid_clusters_rof");
    gsl::span<const ROFRecord> inROFRecords = of::DataRefUtils::as<const ROFRecord>(msgROF);

    std::unique_ptr<const o2::dataformats::MCTruthContainer<MCClusterLabel>> labels = pc.inputs().get<const o2::dataformats::MCTruthContainer<MCClusterLabel>*>("mid_clusterlabels");

    mTracker->process(clusters, inROFRecords);
    mTrackLabeler.process(mTracker->getClusters(), mTracker->getTracks(), *labels);

    pc.outputs().snapshot(of::Output{"MID", "TRACKS", 0, of::Lifetime::Timeframe}, mTracker->getTracks());
    LOG(debug) << "Sent " << mTracker->getTracks().size() << " tracks.";
    pc.outputs().snapshot(of::Output{"MID", "TRACKCLUSTERS", 0, of::Lifetime::Timeframe}, mTracker->getClusters());
    LOG(debug) << "Sent " << mTracker->getClusters().size() << " track clusters.";

    pc.outputs().snapshot(of::Output{"MID", "TRACKROFS", 0, of::Lifetime::Timeframe}, mTracker->getTrackROFRecords());
    LOG(debug) << "Sent " << mTracker->getTrackROFRecords().size() << " ROFs.";
    pc.outputs().snapshot(of::Output{"MID", "TRCLUSROFS", 0, of::Lifetime::Timeframe}, mTracker->getClusterROFRecords());
    LOG(debug) << "Sent " << mTracker->getClusterROFRecords().size() << " ROFs.";

    pc.outputs().snapshot(of::Output{"MID", "TRACKLABELS", 0, of::Lifetime::Timeframe}, mTrackLabeler.getTracksLabels());
    LOG(debug) << "Sent " << mTrackLabeler.getTracksLabels().getIndexedSize() << " indexed tracks.";
    pc.outputs().snapshot(of::Output{"MID", "TRCLUSLABELS", 0, of::Lifetime::Timeframe}, mTrackLabeler.getTrackClustersLabels());
    LOG(debug) << "Sent " << mTrackLabeler.getTrackClustersLabels().getIndexedSize() << " indexed track clusters.";
  }

 private:
  std::unique_ptr<Tracker> mTracker{nullptr};
  TrackLabeler mTrackLabeler{};
};

framework::DataProcessorSpec getTrackerMCSpec()
{

  std::vector<of::InputSpec> inputSpecs{of::InputSpec{"mid_clusters", "MID", "CLUSTERS"}, of::InputSpec{"mid_clusters_rof", "MID", "CLUSTERSROF"}, of::InputSpec{"mid_clusterlabels", "MID", "CLUSTERSLABELS"}};

  std::vector<of::OutputSpec> outputSpecs{
    of::OutputSpec{"MID", "TRACKS"},
    of::OutputSpec{"MID", "TRACKCLUSTERS"},
    of::OutputSpec{"MID", "TRACKROFS"},
    of::OutputSpec{"MID", "TRCLUSROFS"},
    of::OutputSpec{"MID", "TRACKLABELS"},
    of::OutputSpec{"MID", "TRCLUSLABELS"}};

  return of::DataProcessorSpec{
    "TrackerMC",
    {inputSpecs},
    {outputSpecs},
    of::adaptFromTask<o2::mid::TrackerMCDeviceDPL>(),
    of::Options{}};
}
} // namespace mid
} // namespace o2
