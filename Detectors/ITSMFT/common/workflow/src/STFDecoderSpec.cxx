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

/// \file   STFDecoderSpec.cxx
/// \brief  Device to decode ITS or MFT raw data from STF
/// \author ruben.shahoyan@cern.ch

#include <vector>

#include "Framework/WorkflowSpec.h"
#include "Framework/ConfigParamRegistry.h"
#include "Framework/ControlService.h"
#include "Framework/DeviceSpec.h"
#include "DataFormatsITSMFT/Digit.h"
#include "DataFormatsITSMFT/ROFRecord.h"
#include "ITSMFTReconstruction/RawPixelDecoder.h"
#include "ITSMFTReconstruction/Clusterer.h"
#include "ITSMFTReconstruction/ClustererParam.h"
#include "ITSMFTReconstruction/GBTLink.h"
#include "ITSMFTWorkflow/STFDecoderSpec.h"
#include "DetectorsCommonDataFormats/DetectorNameConf.h"
#include "DataFormatsParameters/GRPObject.h"
#include "ITSMFTBase/DPLAlpideParam.h"
#include "DataFormatsITSMFT/CompCluster.h"
#include "DetectorsCommonDataFormats/DetID.h"
#include "CommonUtils/StringUtils.h"

namespace o2
{
namespace itsmft
{

using namespace o2::framework;

///_______________________________________
template <class Mapping>
STFDecoder<Mapping>::STFDecoder(const STFDecoderInp& inp)
  : mDoClusters(inp.doClusters), mDoPatterns(inp.doPatterns), mDoDigits(inp.doDigits), mDoCalibData(inp.doCalib), mAllowReporting(inp.allowReporting), mInputSpec(inp.inputSpec)
{
  mSelfName = o2::utils::Str::concat_string(Mapping::getName(), "STFDecoder");
  mTimer.Stop();
  mTimer.Reset();
}

///_______________________________________
template <class Mapping>
void STFDecoder<Mapping>::init(InitContext& ic)
{
  try {
    mDecoder = std::make_unique<RawPixelDecoder<Mapping>>();
    auto v0 = o2::utils::Str::tokenize(mInputSpec, ':');
    auto v1 = o2::utils::Str::tokenize(v0[1], '/');
    header::DataOrigin dataOrig;
    header::DataDescription dataDesc;
    dataOrig.runtimeInit(v1[0].c_str());
    dataDesc.runtimeInit(v1[1].c_str());
    mDecoder->setUserDataOrigin(dataOrig);
    mDecoder->setUserDataDescription(dataDesc);
    mDecoder->init();
  } catch (const std::exception& e) {
    LOG(error) << "exception was thrown in decoder creation: " << e.what();
    throw;
  } catch (...) {
    LOG(error) << "non-std::exception was thrown in decoder creation";
    throw;
  }

  auto detID = Mapping::getDetID();
  if (detID == o2::detectors::DetID::ITS) {
    mDictName = o2::itsmft::ClustererParam<o2::detectors::DetID::ITS>::Instance().dictFilePath;
    mNoiseName = o2::itsmft::ClustererParam<o2::detectors::DetID::ITS>::Instance().noiseFilePath;
  } else {
    mDictName = o2::itsmft::ClustererParam<o2::detectors::DetID::MFT>::Instance().dictFilePath;
    mNoiseName = o2::itsmft::ClustererParam<o2::detectors::DetID::MFT>::Instance().noiseFilePath;
  }
  mNoiseName = o2::base::DetectorNameConf::getNoiseFileName(detID, mNoiseName, "root");
  mDictName = o2::base::DetectorNameConf::getAlpideClusterDictionaryFileName(detID, mDictName);

  try {
    mNThreads = std::max(1, ic.options().get<int>("nthreads"));
    mDecoder->setNThreads(mNThreads);
    mDecoder->setFormat(ic.options().get<bool>("old-format") ? GBTLink::OldFormat : GBTLink::NewFormat);
    mUnmutExtraLanes = ic.options().get<bool>("unmute-extra-lanes");
    mVerbosity = ic.options().get<int>("decoder-verbosity");
    mDecoder->setFillCalibData(mDoCalibData);
    if (o2::utils::Str::pathExists(mNoiseName)) {
      TFile* f = TFile::Open(mNoiseName.data(), "old");
      auto pnoise = (NoiseMap*)f->Get("ccdb_object");
      AlpideCoder::setNoisyPixels(pnoise);
      LOG(info) << mSelfName << " loading noise map file: " << mNoiseName;
    } else {
      LOG(info) << mSelfName << " Noise file " << mNoiseName << " is absent, " << Mapping::getName() << " running without noise suppression";
    }
  } catch (const std::exception& e) {
    LOG(error) << "exception was thrown in decoder configuration: " << e.what();
    throw;
  } catch (...) {
    LOG(error) << "non-std::exception was thrown in decoder configuration";
    throw;
  }

  if (mDoClusters) {
    try {
      mClusterer = std::make_unique<Clusterer>();
      mClusterer->setNChips(Mapping::getNChips());
      const auto grp = o2::parameters::GRPObject::loadFrom();
      if (grp) {
        mClusterer->setContinuousReadOut(grp->isDetContinuousReadOut(detID));
      } else {
        throw std::runtime_error("failed to retrieve GRP");
      }

      // settings for the fired pixel overflow masking
      const auto& alpParams = DPLAlpideParam<Mapping::getDetID()>::Instance();
      const auto& clParams = ClustererParam<Mapping::getDetID()>::Instance();
      auto nbc = clParams.maxBCDiffToMaskBias;
      nbc += mClusterer->isContinuousReadOut() ? alpParams.roFrameLengthInBC : (alpParams.roFrameLengthTrig / o2::constants::lhc::LHCBunchSpacingNS);
      mClusterer->setMaxBCSeparationToMask(nbc);
      mClusterer->setMaxRowColDiffToMask(clParams.maxRowColDiffToMask);

      if (o2::utils::Str::pathExists(mDictName)) {
        mClusterer->loadDictionary(mDictName);
        LOG(info) << mSelfName << " clusterer running with a provided dictionary: " << mDictName;
      } else {
        LOG(info) << mSelfName << " Dictionary " << mDictName << " is absent, " << Mapping::getName() << " clusterer expects cluster patterns";
      }
      mClusterer->print();
    } catch (const std::exception& e) {
      LOG(error) << "exception was thrown in clustrizer configuration: " << e.what();
      throw;
    } catch (...) {
      LOG(error) << "non-std::exception was thrown in clusterizer configuration";
      throw;
    }
  }
}

///_______________________________________
template <class Mapping>
void STFDecoder<Mapping>::run(ProcessingContext& pc)
{
  static bool firstCall = true;
  if (firstCall) {
    firstCall = false;
    mDecoder->setInstanceID(pc.services().get<const o2::framework::DeviceSpec>().inputTimesliceId);
    mDecoder->setNInstances(pc.services().get<const o2::framework::DeviceSpec>().maxInputTimeslices);
    mDecoder->setVerbosity(mDecoder->getInstanceID() == 0 ? mVerbosity : (mUnmutExtraLanes ? mVerbosity : -1));
    mAllowReporting &= (mDecoder->getInstanceID() == 0) || mUnmutExtraLanes;
  }
  int nSlots = pc.inputs().getNofParts(0);
  double timeCPU0 = mTimer.CpuTime(), timeReal0 = mTimer.RealTime();
  mTimer.Start(false);
  mDecoder->startNewTF(pc.inputs());
  auto orig = Mapping::getOrigin();
  std::vector<o2::itsmft::CompClusterExt> clusCompVec;
  std::vector<o2::itsmft::ROFRecord> clusROFVec;
  std::vector<unsigned char> clusPattVec;

  std::vector<Digit> digVec;
  std::vector<GBTCalibData> calVec;
  std::vector<ROFRecord> digROFVec;

  if (mDoDigits) {
    digVec.reserve(mEstNDig);
    digROFVec.reserve(mEstNROF);
  }
  if (mDoClusters) {
    clusCompVec.reserve(mEstNClus);
    clusROFVec.reserve(mEstNROF);
    clusPattVec.reserve(mEstNClusPatt);
  }
  if (mDoCalibData) {
    calVec.reserve(mEstNCalib);
  }

  mDecoder->setDecodeNextAuto(false);
  while (mDecoder->decodeNextTrigger()) {
    if (mDoDigits) {                                    // call before clusterization, since the latter will hide the digits
      mDecoder->fillDecodedDigits(digVec, digROFVec);   // lot of copying involved
      if (mDoCalibData) {
        mDecoder->fillCalibData(calVec);
      }
    }
    if (mDoClusters) { // !!! THREADS !!!
      mClusterer->process(mNThreads, *mDecoder.get(), &clusCompVec, mDoPatterns ? &clusPattVec : nullptr, &clusROFVec);
    }
  }

  if (mDoDigits) {
    pc.outputs().snapshot(Output{orig, "DIGITS", 0, Lifetime::Timeframe}, digVec);
    pc.outputs().snapshot(Output{orig, "DIGITSROF", 0, Lifetime::Timeframe}, digROFVec);
    mEstNDig = std::max(mEstNDig, size_t(digVec.size() * 1.2));
    mEstNROF = std::max(mEstNROF, size_t(digROFVec.size() * 1.2));
    if (mDoCalibData) {
      pc.outputs().snapshot(Output{orig, "GBTCALIB", 0, Lifetime::Timeframe}, calVec);
      mEstNCalib = std::max(mEstNCalib, size_t(calVec.size() * 1.2));
    }
  }

  if (mDoClusters) {                                                                  // we are not obliged to create vectors which are not requested, but other devices might not know the options of this one
    pc.outputs().snapshot(Output{orig, "COMPCLUSTERS", 0, Lifetime::Timeframe}, clusCompVec);
    pc.outputs().snapshot(Output{orig, "PATTERNS", 0, Lifetime::Timeframe}, clusPattVec);
    pc.outputs().snapshot(Output{orig, "CLUSTERSROF", 0, Lifetime::Timeframe}, clusROFVec);
    mEstNClus = std::max(mEstNClus, size_t(clusCompVec.size() * 1.2));
    mEstNClusPatt = std::max(mEstNClusPatt, size_t(clusPattVec.size() * 1.2));
    mEstNROF = std::max(mEstNROF, size_t(clusROFVec.size() * 1.2));
  }

  if (mDoClusters) {
    LOG(info) << mSelfName << " Built " << clusCompVec.size() << " clusters in " << clusROFVec.size() << " ROFs";
  }
  if (mDoDigits) {
    LOG(info) << mSelfName << " Decoded " << digVec.size() << " Digits in " << digROFVec.size() << " ROFs";
  }
  mTimer.Stop();
  auto tfID = DataRefUtils::getHeader<o2::header::DataHeader*>(pc.inputs().getFirstValid(true))->tfCounter;
  LOG(info) << mSelfName << " Total time for TF " << tfID << '(' << mTFCounter << ") : CPU: " << mTimer.CpuTime() - timeCPU0 << " Real: " << mTimer.RealTime() - timeReal0;
  mTFCounter++;
}
///_______________________________________
template <class Mapping>
void STFDecoder<Mapping>::finalize()
{
  if (mFinalizeDone) {
    return;
  }
  mFinalizeDone = true;
  LOGF(info, "%s statistics:", mSelfName);
  LOGF(info, "%s Total STF decoding%s timing (w/o disk IO): Cpu: %.3e Real: %.3e s in %d slots", mSelfName,
       mDoClusters ? "/clustering" : "", mTimer.CpuTime(), mTimer.RealTime(), mTimer.Counter() - 1);
  if (mDecoder && mAllowReporting) {
    mDecoder->printReport();
  }
  if (mClusterer) {
    mClusterer->print();
  }
}

DataProcessorSpec getSTFDecoderSpec(const STFDecoderInp& inp)
{
  std::vector<OutputSpec> outputs;
  if (inp.doDigits) {
    outputs.emplace_back(inp.origin, "DIGITS", 0, Lifetime::Timeframe);
    outputs.emplace_back(inp.origin, "DIGITSROF", 0, Lifetime::Timeframe);
    if (inp.doCalib) {
      outputs.emplace_back(inp.origin, "GBTCALIB", 0, Lifetime::Timeframe);
    }
  }
  if (inp.doClusters) {
    outputs.emplace_back(inp.origin, "COMPCLUSTERS", 0, Lifetime::Timeframe);
    outputs.emplace_back(inp.origin, "CLUSTERSROF", 0, Lifetime::Timeframe);
    // in principle, we don't need to open this input if we don't need to send real data,
    // but other devices expecting it do not know about options of this device: problem?
    // if (doClusters && doPatterns)
    outputs.emplace_back(inp.origin, "PATTERNS", 0, Lifetime::Timeframe);
  }

  auto inputs = o2::framework::select(inp.inputSpec.c_str());
  if (inp.askSTFDist) {
    for (auto& ins : inputs) { // mark input as optional in order not to block the workflow if our raw data happen to be missing in some TFs
      ins.lifetime = Lifetime::Optional;
    }
    // request the input FLP/DISTSUBTIMEFRAME/0 that is _guaranteed_ to be present, even if none of our raw data is present.
    inputs.emplace_back("stfDist", "FLP", "DISTSUBTIMEFRAME", 0, o2::framework::Lifetime::Timeframe);
  }

  return DataProcessorSpec{
    inp.deviceName,
    inputs,
    outputs,
    inp.origin == o2::header::gDataOriginITS ? AlgorithmSpec{adaptFromTask<STFDecoder<ChipMappingITS>>(inp)} : AlgorithmSpec{adaptFromTask<STFDecoder<ChipMappingMFT>>(inp)},
    Options{
      {"nthreads", VariantType::Int, 1, {"Number of decoding/clustering threads"}},
      {"old-format", VariantType::Bool, false, {"Use old format (1 trigger per CRU page)"}},
      {"decoder-verbosity", VariantType::Int, 0, {"Verbosity level (-1: silent, 0: errors, 1: headers, 2: data) of 1st lane"}},
      {"unmute-extra-lanes", VariantType::Bool, false, {"allow extra lanes to be as verbose as 1st one"}}}};
}

} // namespace itsmft
} // namespace o2
