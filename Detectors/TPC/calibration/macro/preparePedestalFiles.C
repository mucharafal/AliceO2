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

#if !defined(__CLING__) || defined(__ROOTCLING__)
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <tuple>
#include <numeric>

#include "TFile.h"
#include "TROOT.h"
#include "TSystem.h"
#include "TString.h"

#include "TPCBase/CDBInterface.h"
#include "TPCBase/Mapper.h"
#include "TPCBase/CalDet.h"
#include "TPCBase/Utils.h"
#endif

struct LinkInfo {
  LinkInfo(int cru, int link) : cru(cru), globalLinkID(link) {}
  int cru{0};
  int globalLinkID{0};

  bool operator<(const LinkInfo& other) const
  {
    if (cru < other.cru) {
      return true;
    }
    if ((cru == other.cru) && (globalLinkID < other.globalLinkID)) {
      return true;
    }
    return false;
  }
};

using ValueArray = std::array<uint32_t, 80>;
using DataMap = std::map<LinkInfo, ValueArray>;

void writeValues(const std::string_view fileName, const DataMap& map, bool onlyFilled = false);
int getHWChannel(int sampa, int channel, int regionIter);

/// convert float to integer with fixed precision and max number of digits
template <uint32_t DataBitSizeT = 12, uint32_t SignificantBitsT = 2>
constexpr uint32_t floatToFixedSize(float value)
{
  constexpr uint32_t DataBitSize = DataBitSizeT;                       ///< number of bits of the data representation
  constexpr uint32_t SignificantBits = SignificantBitsT;               ///< number of bits used for floating point precision
  constexpr uint64_t BitMask = ((uint64_t(1) << DataBitSize) - 1);     ///< mask for bits
  constexpr float FloatConversion = 1.f / float(1 << SignificantBits); ///< conversion factor from integer representation to float

  const auto adc = uint32_t((value + 0.5f * FloatConversion) / FloatConversion) & BitMask;
  assert(std::abs(value - adc * FloatConversion) <= 0.5f * FloatConversion);

  return adc;
}

template <uint32_t SignificantBitsT = 2>
constexpr float fixedSizeToFloat(uint32_t value)
{
  constexpr uint32_t SignificantBits = SignificantBitsT;               ///< number of bits used for floating point precision
  constexpr float FloatConversion = 1.f / float(1 << SignificantBits); ///< conversion factor from integer representation to float

  return float(value) * FloatConversion;
}

void preparePedestalFiles(const std::string_view pedestalFile, const TString outputDir = "./", float sigmaNoise = 3, float minADC = 2, float pedestalOffset = 0, bool onlyFilled = false, bool maskBad = true, float noisyChannelThreshold = 1.5, float sigmaNoiseNoisyChannels = 4, float badChannelThreshold = 6)
{
  static constexpr float FloatConversion = 1.f / float(1 << 2);

  using namespace o2::tpc;
  const auto& mapper = Mapper::instance();

  // ===| load noise and pedestal from file |===
  CalDet<float> output("Pedestals");
  const CalDet<float>* calPedestal = nullptr;
  const CalDet<float>* calNoise = nullptr;

  if (pedestalFile.find("cdb") != std::string::npos) {
    auto& cdb = CDBInterface::instance();
    if (pedestalFile.find("cdb-test") == 0) {
      cdb.setURL("http://ccdb-test.cern.ch:8080");
    } else if (pedestalFile.find("cdb-prod") == 0) {
      cdb.setURL("http://alice-ccdb.cern.ch");
    }
    const auto timePos = pedestalFile.find("@");
    if (timePos != std::string_view::npos) {
      std::cout << "set time stamp " << std::stol(pedestalFile.substr(timePos + 1).data()) << "\n";
      cdb.setTimeStamp(std::stol(pedestalFile.substr(timePos + 1).data()));
    }
    calPedestal = &cdb.getPedestals();
    calNoise = &cdb.getNoise();
  } else {
    TFile f(pedestalFile.data());
    gROOT->cd();
    f.GetObject("Pedestals", calPedestal);
    f.GetObject("Noise", calNoise);
  }

  DataMap pedestalValues;
  DataMap thresholdlValues;
  DataMap pedestalValuesPhysics;
  DataMap thresholdlValuesPhysics;

  // ===| prepare values |===
  for (size_t iroc = 0; iroc < calPedestal->getData().size(); ++iroc) {
    const ROC roc(iroc);

    const auto& rocPedestal = calPedestal->getCalArray(iroc);
    const auto& rocNoise = calNoise->getCalArray(iroc);
    auto& rocOut = output.getCalArray(iroc);

    const int padOffset = roc.isOROC() ? mapper.getPadsInIROC() : 0;
    const auto& traceLengths = roc.isIROC() ? mapper.getTraceLengthsIROC() : mapper.getTraceLengthsOROC();

    // skip empty
    if (!(std::abs(rocPedestal.getSum() + rocNoise.getSum()) > 0)) {
      continue;
    }

    //loop over pads
    for (size_t ipad = 0; ipad < rocPedestal.getData().size(); ++ipad) {
      const int globalPad = ipad + padOffset;
      const FECInfo& fecInfo = mapper.fecInfo(globalPad);
      const CRU cru = mapper.getCRU(roc.getSector(), globalPad);
      const uint32_t region = cru.region();
      const int cruID = cru.number();
      const int sampa = fecInfo.getSampaChip();
      const int sampaChannel = fecInfo.getSampaChannel();
      //int globalLinkID = fecInfo.getIndex();

      const PartitionInfo& partInfo = mapper.getMapPartitionInfo()[cru.partition()];
      const int nFECs = partInfo.getNumberOfFECs();
      const int fecOffset = (nFECs + 1) / 2;
      const int fecInPartition = fecInfo.getIndex() - partInfo.getSectorFECOffset();
      const int dataWrapperID = fecInPartition >= fecOffset;
      const int globalLinkID = (fecInPartition % fecOffset) + dataWrapperID * 12;

      const auto traceLength = traceLengths[ipad];

      float pedestal = rocPedestal.getValue(ipad);
      if ((pedestal > 0) && (pedestalOffset > pedestal)) {
        printf("ROC: %2zu, pad: %3zu -- pedestal offset %.2f larger than the pedestal value %.2f. Pedestal and noise will be set to 0\n", iroc, ipad, pedestalOffset, pedestal);
      } else {
        pedestal -= pedestalOffset;
      }

      float noise = std::abs(rocNoise.getValue(ipad)); // it seems with the new fitting procedure, the noise can also be negative, since in gaus sigma is quadratic
      float noiseCorr = noise - (0.847601 + 0.031514 * traceLength);
      if ((pedestal <= 0) || (pedestal > 150) || (noise <= 0) || (noise > 50)) {
        printf("Bad pedestal or noise value in ROC %2zu, CRU %3d, fec in CRU: %2d, SAMPA: %d, channel: %2d, pedestal: %.4f, noise %.4f", iroc, cruID, fecInPartition, sampa, sampaChannel, pedestal, noise);
        if (maskBad) {
          pedestal = 1023;
          noise = 1023;
          printf(", they will be masked using pedestal value %.0f and noise %.0f\n", pedestal, noise);
        } else {
          printf(", setting both to 0\n");
          pedestal = 0;
          noise = 0;
        }
      }
      float threshold = (noise > 0) ? std::max(sigmaNoise * noise, minADC) : 0;
      threshold = std::min(threshold, 1023.f);
      float thresholdHighNoise = (noiseCorr > noisyChannelThreshold) ? std::max(sigmaNoiseNoisyChannels * noise, minADC) : threshold;

      float pedestalHighNoise = pedestal;
      if (noiseCorr > badChannelThreshold) {
        pedestalHighNoise = 1023;
        thresholdHighNoise = 1023;
      }

      const int hwChannel = getHWChannel(sampa, sampaChannel, region % 2);
      // for debugging
      //printf("%4d %4d %4d %4d %4d: %u\n", cru.number(), globalLinkID, hwChannel, fecInfo.getSampaChip(), fecInfo.getSampaChannel(), getADCValue(pedestal));

      // default thresholds
      const auto adcPedestal = floatToFixedSize(pedestal);
      const auto adcThreshold = floatToFixedSize(threshold);
      pedestalValues[LinkInfo(cruID, globalLinkID)][hwChannel] = adcPedestal;
      thresholdlValues[LinkInfo(cruID, globalLinkID)][hwChannel] = adcThreshold;

      // higher thresholds for physics data taking
      const auto adcPedestalPhysics = floatToFixedSize(pedestalHighNoise);
      const auto adcThresholdPhysics = floatToFixedSize(thresholdHighNoise);
      pedestalValuesPhysics[LinkInfo(cruID, globalLinkID)][hwChannel] = adcPedestalPhysics;
      thresholdlValuesPhysics[LinkInfo(cruID, globalLinkID)][hwChannel] = adcThresholdPhysics;
      // for debugging
      //if(!(std::abs(pedestal - fixedSizeToFloat(adcPedestal)) <= 0.5 * 0.25)) {
      //printf("%4d %4d %4d %4d %4d: %u %.2f %.4f %.4f\n", cru.number(), globalLinkID, hwChannel, sampa, sampaChannel, adcPedestal, fixedSizeToFloat(adcPedestal), pedestal, pedestal - fixedSizeToFloat(adcPedestal));
      //}
    }
  }

  writeValues((outputDir + "/pedestal_values.txt").Data(), pedestalValues, onlyFilled);
  writeValues((outputDir + "/threshold_values.txt").Data(), thresholdlValues, onlyFilled);

  writeValues((outputDir + "/pedestal_values.physics.txt").Data(), pedestalValuesPhysics, onlyFilled);
  writeValues((outputDir + "/threshold_values.physics.txt").Data(), thresholdlValuesPhysics, onlyFilled);
}

/// return the hardware channel number as mapped in the CRU
//
int getHWChannel(int sampa, int channel, int regionIter)
{
  const int sampaOffet[5] = {0, 4, 8, 0, 4};
  if (regionIter && sampa == 2) {
    channel -= 16;
  }
  int outch = sampaOffet[sampa] + ((channel % 16) % 2) + 2 * (channel / 16) + (channel % 16) / 2 * 10;
  return outch;
}

/// write values of map to fileName
///
void writeValues(const std::string_view fileName, const DataMap& map, bool onlyFilled)
{
  std::ofstream str(fileName.data(), std::ofstream::out);

  for (const auto& [linkInfo, data] : map) {
    if (onlyFilled) {
      if (!std::accumulate(data.begin(), data.end(), uint32_t(0))) {
        continue;
      }
    }
    std::string values;
    for (const auto& val : data) {
      if (values.size()) {
        values += ",";
      }
      values += std::to_string(val);
    }

    str << linkInfo.cru << " "
        << linkInfo.globalLinkID << " "
        << values << "\n";
  }
}

/// convert HW mapping to sampa and channel number
std::tuple<int, int> getSampaInfo(int hwChannel, int cruID)
{
  static constexpr int sampaMapping[10] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 2};
  static constexpr int channelOffset[10] = {0, 16, 0, 16, 0, 0, 16, 0, 16, 16};
  const int regionIter = cruID % 2;

  const int istreamm = ((hwChannel % 10) / 2);
  const int partitionStream = istreamm + regionIter * 5;
  const int sampaOnFEC = sampaMapping[partitionStream];
  const int channel = (hwChannel % 2) + 2 * (hwChannel / 10);
  const int channelOnSAMPA = channel + channelOffset[partitionStream];

  return {sampaOnFEC, channelOnSAMPA};
}

/// Test input channel mapping vs output channel mapping
///
/// Consistency check of mapping
void testChannelMapping(int cruID = 0)
{
  const int sampaMapping[10] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 2};
  const int channelOffset[10] = {0, 16, 0, 16, 0, 0, 16, 0, 16, 16};
  const int regionIter = cruID % 2;

  for (std::size_t ichannel = 0; ichannel < 80; ++ichannel) {
    const int istreamm = ((ichannel % 10) / 2);
    const int partitionStream = istreamm + regionIter * 5;
    const int sampaOnFEC = sampaMapping[partitionStream];
    const int channel = (ichannel % 2) + 2 * (ichannel / 10);
    const int channelOnSAMPA = channel + channelOffset[partitionStream];

    const size_t outch = getHWChannel(sampaOnFEC, channelOnSAMPA, regionIter);
    printf("%4zu %4d %4d : %4zu %s\n", outch, sampaOnFEC, channelOnSAMPA, ichannel, (outch != ichannel) ? "============" : "");
  }
}

/// create cal pad object from HW value file
///
/// if outputFile is set, write the object to file
/// if calPadName is set use it for the object name in the file. Otherwise the basename of the fileName is used
o2::tpc::CalDet<float> getCalPad(const std::string_view fileName, const std::string_view outputFile = "", std::string_view calPadName = "")
{
  using namespace o2::tpc;
  const auto& mapper = Mapper::instance();

  static constexpr float FloatConversion = 1.f / float(1 << 2);

  int cruID{0};
  int globalLinkID{0};
  int sampaOnFEC{0};
  int channelOnSAMPA{0};
  std::string values;
  if (!calPadName.size()) {
    calPadName = gSystem->BaseName(fileName.data());
  }
  CalDet<float> calPad(calPadName);

  std::string line;
  std::ifstream infile(fileName.data(), std::ifstream::in);
  if (!infile.is_open()) {
    std::cout << "could not open file " << fileName << "\n";
    return calPad;
  }

  while (std::getline(infile, line)) {
    std::stringstream streamLine(line);
    streamLine >> cruID >> globalLinkID >> values;

    const CRU cru(cruID);
    const PartitionInfo& partInfo = mapper.getMapPartitionInfo()[cru.partition()];
    const int nFECs = partInfo.getNumberOfFECs();
    const int fecOffset = (nFECs + 1) / 2;
    const int fecInPartition = (globalLinkID < fecOffset) ? globalLinkID : fecOffset + globalLinkID % 12;

    int hwChannel{0};
    for (const auto& val : utils::tokenize(values, ",")) {
      std::tie(sampaOnFEC, channelOnSAMPA) = getSampaInfo(hwChannel, cru);
      const PadROCPos padROCPos = mapper.padROCPos(cru, fecInPartition, sampaOnFEC, channelOnSAMPA);
      const float set = fixedSizeToFloat(uint32_t(std::stoi(val)));
      calPad.getCalArray(padROCPos.getROC()).setValue(padROCPos.getRow(), padROCPos.getPad(), set);
      ++hwChannel;
    }
  }

  if (outputFile.size()) {
    TFile f(outputFile.data(), "recreate");
    f.WriteObject(&calPad, calPadName.data());
  }
  return calPad;
}

/// debug differences between two cal pad objects
void debugDiff(std::string_view file1, std::string_view file2, std::string_view objName)
{
  using namespace o2::tpc;
  CalPad dummy;
  CalPad* calPad1{nullptr};
  CalPad* calPad2{nullptr};

  std::unique_ptr<TFile> tFile1(TFile::Open(file1.data()));
  std::unique_ptr<TFile> tFile2(TFile::Open(file2.data()));
  gROOT->cd();

  tFile1->GetObject(objName.data(), calPad1);
  tFile2->GetObject(objName.data(), calPad2);

  for (size_t iroc = 0; iroc < calPad1->getData().size(); ++iroc) {
    const auto& calArray1 = calPad1->getCalArray(iroc);
    const auto& calArray2 = calPad2->getCalArray(iroc);
    // skip empty
    if (!(std::abs(calArray1.getSum() + calArray2.getSum()) > 0)) {
      continue;
    }

    for (size_t ipad = 0; ipad < calArray1.getData().size(); ++ipad) {
      const auto val1 = calArray1.getValue(ipad);
      const auto val2 = calArray2.getValue(ipad);

      if (std::abs(val2 - val1) >= 0.25) {
        printf("%2zu %5zu : %.5f - %.5f = %.2f\n", iroc, ipad, val2, val1, val2 - val1);
      }
    }
  }
}
