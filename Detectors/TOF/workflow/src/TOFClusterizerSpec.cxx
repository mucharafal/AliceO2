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

#include "TOFWorkflowUtils/TOFClusterizerSpec.h"
#include "Framework/ControlService.h"
#include "Framework/DataProcessorSpec.h"
#include "Framework/DataRefUtils.h"
#include "Framework/Lifetime.h"
#include "Framework/Task.h"
#include "Headers/DataHeader.h"
#include "TOFReconstruction/Clusterer.h"
#include "TOFReconstruction/CosmicProcessor.h"
#include "TOFReconstruction/DataReader.h"
#include "DataFormatsTOF/Cluster.h"
#include "DataFormatsTOF/CosmicInfo.h"
#include "DataFormatsTOF/CalibInfoCluster.h"
#include "SimulationDataFormat/MCTruthContainer.h"
#include "SimulationDataFormat/MCCompLabel.h"
#include "DataFormatsTOF/CalibLHCphaseTOF.h"
#include "DataFormatsTOF/CalibTimeSlewingParamTOF.h"
#include "TOFBase/CalibTOFapi.h"
#include "TStopwatch.h"
#include "Framework/ConfigParamRegistry.h"
#include "Framework/DataRefUtils.h"
#include "TOFBase/Utils.h"
#include "Steer/MCKinematicsReader.h"

#include <memory> // for make_shared, make_unique, unique_ptr
#include <vector>

// RSTODO to remove once the framework will start propagating the header.firstTForbit
#include "DetectorsRaw/HBFUtils.h"

using namespace o2::framework;
using namespace o2::dataformats;

namespace o2
{
namespace tof
{

// use the tasking system of DPL
// just need to implement 2 special methods init + run (there is no need to inherit from anything)
class TOFDPLClustererTask
{
  bool mUseMC = true;
  bool mUseCCDB = false;
  bool mIsCalib = false;
  bool mIsCosmic = false;
  int mTimeWin = 5000;
  std::string mCCDBurl;

 public:
  explicit TOFDPLClustererTask(bool useMC, bool useCCDB, bool doCalib, bool isCosmic, std::string ccdb_url) : mUseMC(useMC), mUseCCDB(useCCDB), mIsCalib(doCalib), mIsCosmic(isCosmic), mCCDBurl(ccdb_url) {}
  void init(framework::InitContext& ic)
  {
    // nothing special to be set up
    mTimer.Stop();
    mTimer.Reset();

    mTimeWin = ic.options().get<int>("cluster-time-window");
    LOG(debug) << "Is calibration from cluster on? " << mIsCalib;
    LOG(debug) << "DeltaTime for clusterization = " << mTimeWin << " ps";
    LOG(debug) << "Is cosmics? " << mIsCosmic;

    mClusterer.setCalibFromCluster(mIsCalib);
    mClusterer.setDeltaTforClustering(mTimeWin);

    // initialize collision context
    auto mcReader = std::make_unique<o2::steer::MCKinematicsReader>("collisioncontext.root");
    auto context = mcReader->getDigitizationContext();
    if (context) {
      auto bcf = context->getBunchFilling();
      std::bitset<3564> isInBC = bcf.getBCPattern();
      for (unsigned int i = 0; i < isInBC.size(); i++) {
        if (isInBC.test(i)) {
          o2::tof::Utils::addInteractionBC(i, true);
        }
      }
    }
  }

  void run(framework::ProcessingContext& pc)
  {
    mTimer.Start(false);
    // get digit data
    auto digits = pc.inputs().get<gsl::span<o2::tof::Digit>>("tofdigits");
    auto row = pc.inputs().get<gsl::span<o2::tof::ReadoutWindowData>>("readoutwin");
    auto dia = pc.inputs().get<o2::tof::Diagnostic*>("diafreq");
    auto patterns = pc.inputs().get<vector<unsigned char>>("patterns");

    const auto* dh = DataRefUtils::getHeader<o2::header::DataHeader*>(pc.inputs().getFirstValid(true));
    mClusterer.setFirstOrbit(dh->firstTForbit);

    auto labelvector = std::make_shared<std::vector<o2::dataformats::MCTruthContainer<o2::MCCompLabel>>>();
    if (mUseMC) {
      auto digitlabels = pc.inputs().get<std::vector<o2::dataformats::MCTruthContainer<o2::MCCompLabel>>*>("tofdigitlabels");
      *labelvector.get() = std::move(*digitlabels);
      mClusterer.setMCTruthContainer(&mClsLabels);
      mClsLabels.clear();
    }

    o2::dataformats::CalibLHCphaseTOF lhcPhaseObj;
    auto channelCalibObj = std::make_unique<o2::dataformats::CalibTimeSlewingParamTOF>();

    /*
    if (mUseCCDB) { // read calibration objects from ccdb
      // check LHC phase
      auto lhcPhase = pc.inputs().get<o2::dataformats::CalibLHCphaseTOF*>("tofccdbLHCphase");
      auto channelCalib = pc.inputs().get<o2::dataformats::CalibTimeSlewingParamTOF*>("tofccdbChannelCalib");

      o2::dataformats::CalibLHCphaseTOF lhcPhaseObjTmp = std::move(*lhcPhase);
      o2::dataformats::CalibTimeSlewingParamTOF channelCalibObjTmp = std::move(*channelCalib);

      // make a copy in global scope
      lhcPhaseObj = lhcPhaseObjTmp;
      *channelCalibObj = channelCalibObjTmp;
    } else { // calibration objects set to zero
*/
    lhcPhaseObj.addLHCphase(0, 0);
    lhcPhaseObj.addLHCphase(2000000000, 0);

    for (int ich = 0; ich < o2::dataformats::CalibTimeSlewingParamTOF::NCHANNELS; ich++) {
      channelCalibObj->addTimeSlewingInfo(ich, 0, 0);
      int sector = ich / o2::dataformats::CalibTimeSlewingParamTOF::NCHANNELXSECTOR;
      int channelInSector = ich % o2::dataformats::CalibTimeSlewingParamTOF::NCHANNELXSECTOR;
      channelCalibObj->setFractionUnderPeak(sector, channelInSector, 1);
    }
    //    }

    o2::tof::CalibTOFapi calibapi(long(0), &lhcPhaseObj, channelCalibObj.get());

    if (mUseCCDB) {
      calibapi.setURL(mCCDBurl.c_str());
      auto creationTime = DataRefUtils::getHeader<DataProcessingHeader*>(pc.inputs().getFirstValid(true))->creation;
      calibapi.setTimeStamp(creationTime / 1000);
      LOG(info) << "CCDB required from TOF clusterizer with timestamp " << creationTime / 1000 << " from URL " << mCCDBurl.c_str();

      LOG(info) << "read LHCphase";
      calibapi.readLHCphase();
      LOG(info) << "read time slewing";
      calibapi.readTimeSlewingParam();
      LOG(info) << "read daignostic";
      calibapi.readDiagnosticFrequencies();
    } else {
      LOG(info) << "No CCDB requested by TOF";
    }

    mClusterer.setCalibApi(&calibapi);

    mClusterer.clearDiagnostic();
    mClusterer.addDiagnostic(*dia);

    // call actual clustering routine
    mClustersArray.clear();
    if (mIsCalib) {
      mClusterer.getInfoFromCluster()->clear();
    }

    if (mIsCosmic) {
      mCosmicProcessor.clear();
    }

    for (unsigned int i = 0; i < row.size(); i++) {
      //fill trm pattern but process them in clusterize since they are for readout windows
      calibapi.resetTRMErrors();

      // loop over crates
      int kw = 0;
      for (int crate = 0; crate < 72; crate++) {
        int slot = -1;
        int nwords = kw + row[i].getDiagnosticInCrate(crate);
        int eword;
        for (; kw < nwords; kw++) {
          if (patterns[kw] > 28) { // new slot
            if (slot > -1) {       // fill previous
              calibapi.processError(crate, slot, eword);
            }
            slot = patterns[kw] - 28;
            eword = 0;
          } else if (slot > -1) { // process error in this slot
            eword += 1 << patterns[kw];
          }
        }
      } // end crate loop

      //printf("# TOF readout window for clusterization = %d/%lu (N digits = %d)\n", i, row.size(), row[i].size());
      auto digitsRO = row[i].getBunchChannelData(digits);
      mReader.setDigitArray(&digitsRO);

      if (mIsCosmic) {
        mCosmicProcessor.process(mReader, i != 0);
      }

      if (mUseMC) {
        mClusterer.process(mReader, mClustersArray, &(labelvector->at(i)));
      } else {
        mClusterer.process(mReader, mClustersArray, nullptr);
      }
    }
    LOG(debug) << "TOF CLUSTERER : TRANSFORMED " << digits.size()
               << " DIGITS TO " << mClustersArray.size() << " CLUSTERS";

    // send clusters
    pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "CLUSTERS", 0, Lifetime::Timeframe}, mClustersArray);
    // send labels
    if (mUseMC) {
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "CLUSTERSMCTR", 0, Lifetime::Timeframe}, mClsLabels);
    }

    if (mIsCalib) {
      std::vector<CalibInfoCluster>* clusterCalInfo = mClusterer.getInfoFromCluster();
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "INFOCALCLUS", 0, Lifetime::Timeframe}, *clusterCalInfo);
    }

    if (mIsCosmic) {
      std::vector<CosmicInfo>* cosmicInfo = mCosmicProcessor.getCosmicInfo();
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "INFOCOSMICS", 0, Lifetime::Timeframe}, *cosmicInfo);
      std::vector<CalibInfoTrackCl>* cosmicTrack = mCosmicProcessor.getCosmicTrack();
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "INFOTRACKCOS", 0, Lifetime::Timeframe}, *cosmicTrack);
      std::vector<int>* cosmicTrackSize = mCosmicProcessor.getCosmicTrackSize();
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "INFOTRACKSIZE", 0, Lifetime::Timeframe}, *cosmicTrackSize);
    }

    mTimer.Stop();
  }

  void endOfStream(EndOfStreamContext& ec)
  {
    LOGF(debug, "TOF Clusterer total timing: Cpu: %.3e Real: %.3e s in %d slots",
         mTimer.CpuTime(), mTimer.RealTime(), mTimer.Counter() - 1);
  }

 private:
  DigitDataReader mReader; ///< Digit reader
  Clusterer mClusterer;    ///< Cluster finder
  CosmicProcessor mCosmicProcessor; ///< Cosmics finder
  TStopwatch mTimer;

  std::vector<Cluster> mClustersArray; ///< Array of clusters
  MCLabelContainer mClsLabels;
  std::vector<CalibInfoCluster> mClusterCalInfo; ///< Array of clusters
};

o2::framework::DataProcessorSpec getTOFClusterizerSpec(bool useMC, bool useCCDB, bool doCalib, bool isCosmic, std::string ccdb_url)
{
  std::vector<InputSpec> inputs;
  inputs.emplace_back("tofdigits", o2::header::gDataOriginTOF, "DIGITS", 0, Lifetime::Timeframe);
  inputs.emplace_back("readoutwin", o2::header::gDataOriginTOF, "READOUTWINDOW", 0, Lifetime::Timeframe);
  inputs.emplace_back("diafreq", o2::header::gDataOriginTOF, "DIAFREQ", 0, Lifetime::Timeframe);
  inputs.emplace_back("patterns", o2::header::gDataOriginTOF, "PATTERNS", 0, Lifetime::Timeframe);

  //  if (useCCDB) {
  //    inputs.emplace_back("tofccdbLHCphase", o2::header::gDataOriginTOF, "LHCphase");
  //    inputs.emplace_back("tofccdbChannelCalib", o2::header::gDataOriginTOF, "ChannelCalib");
  //  }
  if (useMC) {
    inputs.emplace_back("tofdigitlabels", o2::header::gDataOriginTOF, "DIGITSMCTR", 0, Lifetime::Timeframe);
  }

  std::vector<OutputSpec> outputs;
  outputs.emplace_back(o2::header::gDataOriginTOF, "CLUSTERS", 0, Lifetime::Timeframe);
  if (useMC) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "CLUSTERSMCTR", 0, Lifetime::Timeframe);
  }

  if (doCalib) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "INFOCALCLUS", 0, Lifetime::Timeframe);
  }
  if (isCosmic) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "INFOCOSMICS", 0, Lifetime::Timeframe);
    outputs.emplace_back(o2::header::gDataOriginTOF, "INFOTRACKCOS", 0, Lifetime::Timeframe);
    outputs.emplace_back(o2::header::gDataOriginTOF, "INFOTRACKSIZE", 0, Lifetime::Timeframe);
  }

  return DataProcessorSpec{
    "TOFClusterer",
    inputs,
    outputs,
    AlgorithmSpec{adaptFromTask<TOFDPLClustererTask>(useMC, useCCDB, doCalib, isCosmic, ccdb_url)},
    Options{{"cluster-time-window", VariantType::Int, 5000, {"time window for clusterization in ps"}}}};
}

} // end namespace tof
} // end namespace o2
