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

/// @file   CookedTrackerSpec.cxx

#include <vector>

#include "TGeoGlobalMagField.h"

#include "Framework/ControlService.h"
#include "Framework/ConfigParamRegistry.h"
#include "ITSWorkflow/CookedTrackerSpec.h"
#include "DataFormatsITSMFT/CompCluster.h"
#include "DataFormatsITSMFT/Cluster.h"
#include "DataFormatsITS/TrackITS.h"
#include "SimulationDataFormat/MCCompLabel.h"
#include "SimulationDataFormat/MCTruthContainer.h"
#include "DataFormatsITSMFT/ROFRecord.h"
#include "ITSMFTBase/DPLAlpideParam.h"

#include "Field/MagneticField.h"
#include "DetectorsBase/GeometryManager.h"
#include "DetectorsBase/Propagator.h"
#include "ITSBase/GeometryTGeo.h"
#include "CommonDataFormat/IRFrame.h"
#include "ITStracking/ROframe.h"
#include "ITStracking/IOUtils.h"
#include "DetectorsCommonDataFormats/DetectorNameConf.h"
#include "CommonUtils/StringUtils.h"

#include "ITSReconstruction/FastMultEstConfig.h"
#include "ITSReconstruction/FastMultEst.h"
#include "ITSMFTReconstruction/ClustererParam.h"

using namespace o2::framework;

namespace o2
{
namespace its
{

using Vertex = o2::dataformats::Vertex<o2::dataformats::TimeStamp<int>>;

CookedTrackerDPL::CookedTrackerDPL(bool useMC, const std::string& trMode) : mUseMC(useMC)
{
  if (trMode == "cosmics") {
    LOG(info) << "Tracking mode \"cosmics\"";
    mTracker.setParametersCosmics();
    mRunVertexer = false;
  }
  mVertexerTraitsPtr = std::make_unique<VertexerTraits>();
  mVertexerPtr = std::make_unique<Vertexer>(mVertexerTraitsPtr.get());
}

void CookedTrackerDPL::init(InitContext& ic)
{
  mTimer.Stop();
  mTimer.Reset();
  auto nthreads = ic.options().get<int>("nthreads");
  mTracker.setNumberOfThreads(nthreads);
  auto filename = ic.options().get<std::string>("grp-file");
  const auto grp = o2::parameters::GRPObject::loadFrom(filename);
  if (grp) {
    mVertexerPtr->getGlobalConfiguration();

    mGRP.reset(grp);
    o2::base::Propagator::initFieldFromGRP(grp);
    auto field = static_cast<o2::field::MagneticField*>(TGeoGlobalMagField::Instance()->GetField());

    o2::base::GeometryManager::loadGeometry();
    o2::its::GeometryTGeo* geom = o2::its::GeometryTGeo::Instance();
    geom->fillMatrixCache(o2::math_utils::bit2Mask(o2::math_utils::TransformType::T2L, o2::math_utils::TransformType::T2GRot,
                                                   o2::math_utils::TransformType::T2G));
    mTracker.setGeometry(geom);

    mTracker.setConfigParams();

    double origD[3] = {0., 0., 0.};
    mTracker.setBz(field->getBz(origD));

    bool continuous = mGRP->isDetContinuousReadOut("ITS");
    LOG(info) << "ITSCookedTracker RO: continuous=" << continuous;
    mTracker.setContinuousMode(continuous);
  } else {
    throw std::runtime_error(o2::utils::Str::concat_string("Cannot retrieve GRP from the ", filename));
  }

  std::string dictPath = o2::itsmft::ClustererParam<o2::detectors::DetID::ITS>::Instance().dictFilePath;
  std::string dictFile = o2::base::DetectorNameConf::getAlpideClusterDictionaryFileName(o2::detectors::DetID::ITS, dictPath);
  if (o2::utils::Str::pathExists(dictFile)) {
    mDict.readFromFile(dictFile);
    LOG(info) << "Tracker running with a provided dictionary: " << dictFile;
  } else {
    LOG(info) << "Dictionary " << dictFile << " is absent, Tracker expects cluster patterns";
  }
}

void CookedTrackerDPL::run(ProcessingContext& pc)
{
  mTimer.Start(false);
  auto compClusters = pc.inputs().get<gsl::span<o2::itsmft::CompClusterExt>>("compClusters");
  gsl::span<const unsigned char> patterns = pc.inputs().get<gsl::span<unsigned char>>("patterns");

  // code further down does assignment to the rofs and the altered object is used for output
  // we therefore need a copy of the vector rather than an object created directly on the input data,
  // the output vector however is created directly inside the message memory thus avoiding copy by
  // snapshot
  auto rofsinput = pc.inputs().get<gsl::span<o2::itsmft::ROFRecord>>("ROframes");
  auto& rofs = pc.outputs().make<std::vector<o2::itsmft::ROFRecord>>(Output{"ITS", "ITSTrackROF", 0, Lifetime::Timeframe}, rofsinput.begin(), rofsinput.end());

  std::unique_ptr<const o2::dataformats::MCTruthContainer<o2::MCCompLabel>> labels;
  gsl::span<itsmft::MC2ROFRecord const> mc2rofs;
  if (mUseMC) {
    labels = pc.inputs().get<const o2::dataformats::MCTruthContainer<o2::MCCompLabel>*>("labels");
    // get the array as read-onlt span, a snapshot is send forward
    mc2rofs = pc.inputs().get<gsl::span<itsmft::MC2ROFRecord>>("MC2ROframes");
  }
  const auto& multEstConf = FastMultEstConfig::Instance(); // parameters for mult estimation and cuts
  FastMultEst multEst;                                     // mult estimator

  LOG(info) << "ITSCookedTracker pulled " << compClusters.size() << " clusters, in " << rofs.size() << " RO frames";

  std::vector<o2::MCCompLabel> trackLabels;
  if (mUseMC) {
    mTracker.setMCTruthContainers(labels.get(), &trackLabels);
  }

  o2::its::ROframe event(0, 7);

  auto& vertROFvec = pc.outputs().make<std::vector<o2::itsmft::ROFRecord>>(Output{"ITS", "VERTICESROF", 0, Lifetime::Timeframe});
  auto& vertices = pc.outputs().make<std::vector<Vertex>>(Output{"ITS", "VERTICES", 0, Lifetime::Timeframe});
  auto& tracks = pc.outputs().make<std::vector<o2::its::TrackITS>>(Output{"ITS", "TRACKS", 0, Lifetime::Timeframe});
  auto& clusIdx = pc.outputs().make<std::vector<int>>(Output{"ITS", "TRACKCLSID", 0, Lifetime::Timeframe});
  auto& irFrames = pc.outputs().make<std::vector<o2::dataformats::IRFrame>>(Output{"ITS", "IRFRAMES", 0, Lifetime::Timeframe});

  const auto& alpParams = o2::itsmft::DPLAlpideParam<o2::detectors::DetID::ITS>::Instance(); // RS: this should come from CCDB
  int nBCPerTF = mTracker.getContinuousMode() ? alpParams.roFrameLengthInBC : alpParams.roFrameLengthTrig;

  gsl::span<const unsigned char>::iterator pattIt = patterns.begin();
  for (auto& rof : rofs) {
    auto& vtxROF = vertROFvec.emplace_back(rof); // register entry and number of vertices in the
    vtxROF.setFirstEntry(vertices.size());
    vtxROF.setNEntries(0);

    auto it = pattIt;

    // fast cluster mult. cut if asked (e.g. sync. mode)
    if (rof.getNEntries() && (multEstConf.cutMultClusLow > 0 || multEstConf.cutMultClusHigh > 0)) { // cut was requested
      auto mult = multEst.process(rof.getROFData(compClusters));
      if (mult < multEstConf.cutMultClusLow || (multEstConf.cutMultClusHigh > 0 && mult > multEstConf.cutMultClusHigh)) {
        LOG(info) << "Estimated cluster mult. " << mult << " is outside of requested range "
                  << multEstConf.cutMultClusLow << " : " << multEstConf.cutMultClusHigh << " | ROF " << rof.getBCData();
        rof.setFirstEntry(tracks.size());
        rof.setNEntries(0);
        continue;
      }
    }

    if (mRunVertexer) {
      o2::its::ioutils::loadROFrameData(rof, event, compClusters, pattIt, mDict, labels.get());
      mVertexerPtr->clustersToVertices(event, false, [&](std::string s) { LOG(info) << s; });
    }
    auto vtxVecLoc = mVertexerPtr->exportVertices();

    if (multEstConf.cutMultVtxLow > 0 || multEstConf.cutMultVtxHigh > 0) { // cut was requested
      std::vector<o2::dataformats::Vertex<o2::dataformats::TimeStamp<int>>> vtxVecSel;
      vtxVecSel.swap(vtxVecLoc);
      for (const auto& vtx : vtxVecSel) {
        if (vtx.getNContributors() < multEstConf.cutMultVtxLow || (multEstConf.cutMultVtxHigh > 0 && vtx.getNContributors() > multEstConf.cutMultVtxHigh)) {
          LOG(info) << "Found vertex mult. " << vtx.getNContributors() << " is outside of requested range "
                    << multEstConf.cutMultVtxLow << " : " << multEstConf.cutMultVtxHigh << " | ROF " << rof.getBCData();
          continue; // skip vertex of unwanted multiplicity
        }
        vtxVecLoc.push_back(vtx);
      }
    }
    if (vtxVecLoc.empty()) {
      if (multEstConf.cutMultVtxLow < 1) { // do blind search only if there is no cut on the low mult vertices
        vtxVecLoc.emplace_back();
      } else {
        rof.setFirstEntry(tracks.size());
        rof.setNEntries(0);
        continue;
      }
    } else { // save vetrices
      vtxROF.setNEntries(vtxVecLoc.size());
      for (const auto& vtx : vtxVecLoc) {
        vertices.push_back(vtx);
      }
    }
    mTracker.setVertices(vtxVecLoc);
    mTracker.process(compClusters, it, mDict, tracks, clusIdx, rof);
    if (tracks.size()) {
      irFrames.emplace_back(rof.getBCData(), rof.getBCData() + nBCPerTF - 1);
    }
  }

  LOG(info) << "ITSCookedTracker pushed " << tracks.size() << " tracks";

  if (mUseMC) {
    pc.outputs().snapshot(Output{"ITS", "TRACKSMCTR", 0, Lifetime::Timeframe}, trackLabels);
    pc.outputs().snapshot(Output{"ITS", "ITSTrackMC2ROF", 0, Lifetime::Timeframe}, mc2rofs);
  }
  mTimer.Stop();
}

void CookedTrackerDPL::endOfStream(EndOfStreamContext& ec)
{
  LOGF(info, "ITS Cooked-Tracker total timing: Cpu: %.3e Real: %.3e s in %d slots",
       mTimer.CpuTime(), mTimer.RealTime(), mTimer.Counter() - 1);
}

DataProcessorSpec getCookedTrackerSpec(bool useMC, const std::string& trMode)
{
  std::vector<InputSpec> inputs;
  inputs.emplace_back("compClusters", "ITS", "COMPCLUSTERS", 0, Lifetime::Timeframe);
  inputs.emplace_back("patterns", "ITS", "PATTERNS", 0, Lifetime::Timeframe);
  inputs.emplace_back("ROframes", "ITS", "CLUSTERSROF", 0, Lifetime::Timeframe);

  std::vector<OutputSpec> outputs;
  outputs.emplace_back("ITS", "TRACKS", 0, Lifetime::Timeframe);
  outputs.emplace_back("ITS", "TRACKCLSID", 0, Lifetime::Timeframe);
  outputs.emplace_back("ITS", "ITSTrackROF", 0, Lifetime::Timeframe);
  outputs.emplace_back("ITS", "VERTICES", 0, Lifetime::Timeframe);
  outputs.emplace_back("ITS", "VERTICESROF", 0, Lifetime::Timeframe);
  outputs.emplace_back("ITS", "IRFRAMES", 0, Lifetime::Timeframe);

  if (useMC) {
    inputs.emplace_back("labels", "ITS", "CLUSTERSMCTR", 0, Lifetime::Timeframe);
    inputs.emplace_back("MC2ROframes", "ITS", "CLUSTERSMC2ROF", 0, Lifetime::Timeframe);
    outputs.emplace_back("ITS", "TRACKSMCTR", 0, Lifetime::Timeframe);
    outputs.emplace_back("ITS", "ITSTrackMC2ROF", 0, Lifetime::Timeframe);
  }

  return DataProcessorSpec{
    "its-cooked-tracker",
    inputs,
    outputs,
    AlgorithmSpec{adaptFromTask<CookedTrackerDPL>(useMC, trMode)},
    Options{
      {"grp-file", VariantType::String, "o2sim_grp.root", {"Name of the grp file"}},
      {"nthreads", VariantType::Int, 1, {"Number of threads"}}}};
}

} // namespace its
} // namespace o2
