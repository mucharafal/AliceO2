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

/// \file
/// \author Julian Myrcha

#include "EveWorkflow/O2DPLDisplay.h"
#include "EveWorkflow/EveWorkflowHelper.h"
#include "DetectorsBase/GeometryManager.h"
#include "DetectorsBase/Propagator.h"
#include "DataFormatsParameters/GRPObject.h"
#include "DataFormatsGlobalTracking/RecoContainer.h"
#include "DataFormatsTPC/WorkflowHelper.h"
#include "DataFormatsITSMFT/TopologyDictionary.h"
#include "DetectorsCommonDataFormats/DetectorNameConf.h"
#include "ITSBase/GeometryTGeo.h"
#include "ITSMFTReconstruction/ClustererParam.h"
#include "TRDBase/GeometryFlat.h"
#include "TOFBase/Geo.h"
#include "TPCFastTransform.h"
#include "TRDBase/Geometry.h"
#include "GlobalTrackingWorkflowHelpers/InputHelper.h"
#include "ReconstructionDataFormats/GlobalTrackID.h"
#include "Framework/ConfigParamSpec.h"
#include "DataFormatsMCH/TrackMCH.h"
#include "DataFormatsMCH/ROFRecord.h"
#include "DataFormatsMCH/Cluster.h"
#include "MCHTracking/TrackParam.h"
#include <unistd.h>
#include <climits>

using namespace o2::event_visualisation;
using namespace o2::framework;
using namespace o2::dataformats;
using namespace o2::globaltracking;
using namespace o2::tpc;
using namespace o2::trd;

void customize(std::vector<ConfigParamSpec>& workflowOptions)
{
  std::vector<o2::framework::ConfigParamSpec> options{
    {"jsons-folder", VariantType::String, "jsons", {"name of the folder to store json files"}},
    {"eve-hostname", VariantType::String, "", {"name of the host allowed to produce files (empty means no limit)"}},
    {"eve-dds-collection-index", VariantType::Int, -1, {"number of dpl collection allowed to produce files (-1 means no limit)"}},
    {"number-of_files", VariantType::Int, 300, {"maximum number of json files in folder"}},
    {"number-of_tracks", VariantType::Int, -1, {"maximum number of track stored in json file (-1 means no limit)"}},
    {"time-interval", VariantType::Int, 5000, {"time interval in milliseconds between stored files"}},
    {"enable-mc", o2::framework::VariantType::Bool, false, {"enable visualization of MC data"}},
    {"disable-mc", o2::framework::VariantType::Bool, false, {"disable visualization of MC data"}}, // for compatibility, overrides enable-mc
    {"display-clusters", VariantType::String, "ITS,TPC,TRD,TOF", {"comma-separated list of clusters to display"}},
    {"display-tracks", VariantType::String, "TPC,ITS,ITS-TPC,TPC-TRD,ITS-TPC-TRD,TPC-TOF,ITS-TPC-TOF", {"comma-separated list of tracks to display"}},
    {"read-from-files", o2::framework::VariantType::Bool, false, {"comma-separated list of tracks to display"}},
    {"disable-root-input", o2::framework::VariantType::Bool, false, {"Disable root input overriding read-from-files"}},
    {"configKeyValues", VariantType::String, "", {"Semicolon separated key=value strings ..."}}};

  std::swap(workflowOptions, options);
}

#include "Framework/runDataProcessing.h" // main method must be included here (otherwise customize not used)
void O2DPLDisplaySpec::init(InitContext& ic)
{
  LOG(info) << "------------------------    O2DPLDisplay::init version " << this->mWorkflowVersion << "    ------------------------------------";
  mData.init();

  mData.mConfig->configProcessing.runMC = mUseMC;
}

void O2DPLDisplaySpec::run(ProcessingContext& pc)
{
  if (!this->mEveHostNameMatch) {
    return;
  }
  LOG(info) << "------------------------    O2DPLDisplay::run version " << this->mWorkflowVersion << "    ------------------------------------";
  // filtering out any run which occur before reaching next time interval
  auto currentTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = currentTime - this->mTimeStamp;
  if (elapsed < this->mTimeInteval) {
    return; // skip this run - it is too often
  }
  this->mTimeStamp = currentTime;

  EveWorkflowHelper helper;
  helper.getRecoContainer().collectData(pc, *mDataRequest);
  helper.selectTracks(&(mData.mConfig->configCalib), mClMask, mTrkMask, mTrkMask);

  helper.prepareITSClusters(mData.mITSDict);
  helper.prepareMFTClusters(mData.mMFTDict);

  helper.draw(this->mJsonPath, this->mNumberOfFiles, this->mNumberOfTracks, this->mTrkMask, this->mClMask, this->mWorkflowVersion);
  const auto* dh = DataRefUtils::getHeader<o2::header::DataHeader*>(pc.inputs().getFirstValid(true));
  auto endTime = std::chrono::high_resolution_clock::now();
  LOGP(info, "Visualization of TF:{} at orbit {} took {} s.", dh->tfCounter, dh->firstTForbit, std::chrono::duration_cast<std::chrono::microseconds>(endTime - currentTime).count() * 1e-6);
}

void O2DPLDisplaySpec::endOfStream(EndOfStreamContext& ec)
{
}

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  LOG(info) << "------------------------    defineDataProcessing " << O2DPLDisplaySpec::mWorkflowVersion << "    ------------------------------------";

  WorkflowSpec specs;

  std::string jsonFolder = cfgc.options().get<std::string>("jsons-folder");
  std::string eveHostName = cfgc.options().get<std::string>("eve-hostname");
  o2::conf::ConfigurableParam::updateFromString(cfgc.options().get<std::string>("configKeyValues"));
  bool useMC = cfgc.options().get<bool>("enable-mc") && !cfgc.options().get<bool>("disable-mc");

  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  bool eveHostNameMatch = eveHostName.empty() || eveHostName == hostname;

  int eveDDSColIdx = cfgc.options().get<int>("eve-dds-collection-index");
  if (eveDDSColIdx != -1) {
    char* colIdx = getenv("DDS_COLLECTION_INDEX");
    int myIdx = colIdx ? atoi(colIdx) : -1;
    if (myIdx == eveDDSColIdx) {
      LOG(important) << "Restricting DPL Display to collection index, my index " << myIdx << ", enabled " << int(myIdx == eveDDSColIdx);
    } else {
      LOG(info) << "Restricting DPL Display to collection index, my index " << myIdx << ", enabled " << int(myIdx == eveDDSColIdx);
    }
    eveHostNameMatch &= myIdx == eveDDSColIdx;
  }

  std::chrono::milliseconds timeInterval(cfgc.options().get<int>("time-interval"));
  int numberOfFiles = cfgc.options().get<int>("number-of_files");
  int numberOfTracks = cfgc.options().get<int>("number-of_tracks");

  GID::mask_t allowedTracks = GID::getSourcesMask("ITS,TPC,MFT,MCH,ITS-TPC,ITS-TPC-TOF,TPC-TRD,ITS-TPC-TRD,MID");
  GID::mask_t allowedClusters = GID::getSourcesMask("ITS,TPC,MFT,MCH,TRD,TOF,MID,TRD");

  GlobalTrackID::mask_t srcTrk = GlobalTrackID::getSourcesMask(cfgc.options().get<std::string>("display-tracks")) & allowedTracks;
  GlobalTrackID::mask_t srcCl = GlobalTrackID::getSourcesMask(cfgc.options().get<std::string>("display-clusters")) & allowedClusters;
  if (!srcTrk.any() && !srcCl.any()) {
    throw std::runtime_error("No input configured");
  }
  std::shared_ptr<DataRequest> dataRequest = std::make_shared<DataRequest>();
  dataRequest->requestTracks(srcTrk, useMC);
  dataRequest->requestClusters(srcCl, useMC);

  if (cfgc.options().get<bool>("read-from-files")) {
    InputHelper::addInputSpecs(cfgc, specs, srcCl, srcTrk, srcTrk, useMC);
  }

  specs.emplace_back(DataProcessorSpec{
    "o2-eve-display",
    dataRequest->inputs,
    {},
    AlgorithmSpec{adaptFromTask<O2DPLDisplaySpec>(useMC, srcTrk, srcCl, dataRequest, jsonFolder, timeInterval, numberOfFiles, numberOfTracks, eveHostNameMatch)}});

  return std::move(specs);
}
