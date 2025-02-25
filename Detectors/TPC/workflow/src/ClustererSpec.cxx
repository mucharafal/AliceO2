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

/// @file   ClustererSpec.cxx
/// @author Matthias Richter
/// @since  2018-03-23
/// @brief  spec definition for a TPC clusterer process

#include "TPCWorkflow/ClustererSpec.h"
#include "Framework/ControlService.h"
#include "Framework/InputRecordWalker.h"
#include "Headers/DataHeader.h"
#include "DataFormatsTPC/Digit.h"
#include "TPCReconstruction/HwClusterer.h"
#include "TPCBase/Sector.h"
#include "DataFormatsTPC/TPCSectorHeader.h"
#include "SimulationDataFormat/MCTruthContainer.h"
#include "SimulationDataFormat/MCCompLabel.h"
#include <FairMQLogger.h>
#include <memory> // for make_shared
#include <vector>
#include <map>
#include <numeric>   // std::accumulate
#include <algorithm> // std::copy

using namespace o2::framework;
using namespace o2::header;
using namespace o2::dataformats;

namespace o2
{
namespace tpc
{

/// create a processor spec
/// runs the TPC HwClusterer in a DPL process with digits and mc as input
DataProcessorSpec getClustererSpec(bool sendMC)
{
  std::string processorName = "tpc-clusterer";

  constexpr static size_t NSectors = o2::tpc::Sector::MAXSECTOR;
  struct ProcessAttributes {
    std::vector<o2::tpc::ClusterHardwareContainer8kb> clusterArray;
    MCLabelContainer mctruthArray;
    std::array<std::shared_ptr<o2::tpc::HwClusterer>, NSectors> clusterers;
    int verbosity = 1;
    bool sendMC = false;
  };

  auto initFunction = [sendMC](InitContext& ic) {
    // FIXME: the clusterer needs to be initialized with the sector number, so we need one
    // per sector. Taking a closer look to the HwClusterer, the sector number is only used
    // for calculating the CRU id. This could be achieved by passing the current sector as
    // parameter to the clusterer processing function.
    auto processAttributes = std::make_shared<ProcessAttributes>();
    processAttributes->sendMC = sendMC;

    auto processSectorFunction = [processAttributes](ProcessingContext& pc, DataRef const& dataref, DataRef const& mclabelref) {
      auto& clusterArray = processAttributes->clusterArray;
      auto& mctruthArray = processAttributes->mctruthArray;
      auto& clusterers = processAttributes->clusterers;
      auto& verbosity = processAttributes->verbosity;
      auto const* sectorHeader = DataRefUtils::getHeader<o2::tpc::TPCSectorHeader*>(dataref);
      if (sectorHeader == nullptr) {
        LOG(error) << "sector header missing on header stack";
        return;
      }
      auto const* dataHeader = DataRefUtils::getHeader<o2::header::DataHeader*>(dataref);
      o2::header::DataHeader::SubSpecificationType fanSpec = dataHeader->subSpecification;

      const auto sector = sectorHeader->sector();
      if (sector < 0) {
        // forward the control information
        // FIXME define and use flags in TPCSectorHeader
        o2::tpc::TPCSectorHeader header{sector};
        pc.outputs().snapshot(Output{gDataOriginTPC, "CLUSTERHW", fanSpec, Lifetime::Timeframe, {header}}, fanSpec);
        if (DataRefUtils::isValid(mclabelref)) {
          pc.outputs().snapshot(Output{gDataOriginTPC, "CLUSTERHWMCLBL", fanSpec, Lifetime::Timeframe, {header}}, fanSpec);
        }
        return;
      }
      ConstMCLabelContainerView inMCLabels;
      if (DataRefUtils::isValid(mclabelref)) {
        inMCLabels = pc.inputs().get<gsl::span<char>>(mclabelref);
      }
      auto inDigits = pc.inputs().get<gsl::span<o2::tpc::Digit>>(dataref);
      if (verbosity > 0 && inMCLabels.getBuffer().size()) {
        LOG(info) << "received " << inDigits.size() << " digits, "
                  << inMCLabels.getIndexedSize() << " MC label objects"
                  << " input MC label size " << DataRefUtils::getPayloadSize(mclabelref);
      }
      if (!clusterers[sector]) {
        // create the clusterer for this sector, take the same target arrays for all clusterers
        // as they are not invoked in parallel
        // the cost of creating the clusterer should be small so we do it in the processing
        clusterers[sector] = std::make_shared<o2::tpc::HwClusterer>(&clusterArray, sector, &mctruthArray);
        clusterers[sector]->init();
      }
      auto& clusterer = clusterers[sector];

      if (verbosity > 0) {
        LOG(info) << "processing " << inDigits.size() << " digit object(s) of sector " << sectorHeader->sector()
                  << " input size " << DataRefUtils::getPayloadSize(dataref);
      }
      // process the digits and MC labels, the bool parameter controls whether to clear all
      // internal data or not. Have to clear it inside the process method as not only the containers
      // are cleared but also the cluster counter. Clearing the containers externally leaves the
      // cluster counter unchanged and leads to an inconsistency between cluster container and
      // MC label container (the latter just grows with every call).
      clusterer->process(inDigits, inMCLabels, true /* clear output containers and cluster counter */);
      const std::vector<o2::tpc::Digit> emptyDigits;
      ConstMCLabelContainerView emptyLabels;
      clusterer->finishProcess(emptyDigits, emptyLabels, false); // keep here the false, otherwise the clusters are lost of they are not stored in the meantime
      if (verbosity > 0) {
        LOG(info) << "clusterer produced "
                  << std::accumulate(clusterArray.begin(), clusterArray.end(), size_t(0), [](size_t l, auto const& r) { return l + r.getContainer()->numberOfClusters; })
                  << " cluster(s)"
                  << " for sector " << sectorHeader->sector()
                  << " total size " << sizeof(ClusterHardwareContainer8kb) * clusterArray.size();
        if (DataRefUtils::isValid(mclabelref)) {
          LOG(info) << "clusterer produced " << mctruthArray.getIndexedSize() << " MC label object(s) for sector " << sectorHeader->sector();
        }
      }
      // FIXME: that should be a case for pmr, want to send the content of the vector as a binary
      // block by using move semantics
      auto outputPages = pc.outputs().make<ClusterHardwareContainer8kb>(Output{gDataOriginTPC, "CLUSTERHW", fanSpec, Lifetime::Timeframe, {*sectorHeader}}, clusterArray.size());
      std::copy(clusterArray.begin(), clusterArray.end(), outputPages.begin());
      if (DataRefUtils::isValid(mclabelref)) {
        ConstMCLabelContainer mcflat;
        mctruthArray.flatten_to(mcflat);
        pc.outputs().snapshot(Output{gDataOriginTPC, "CLUSTERHWMCLBL", fanSpec, Lifetime::Timeframe, {*sectorHeader}}, mcflat);
      }
    };

    auto processingFct = [processAttributes, processSectorFunction](ProcessingContext& pc) {
      struct SectorInputDesc {
        DataRef dataref;
        DataRef mclabelref;
      };
      // loop over all inputs and their parts and associate data with corresponding mc truth data
      // by the subspecification
      std::map<int, SectorInputDesc> inputs;
      std::vector<InputSpec> filter = {
        {"check", ConcreteDataTypeMatcher{gDataOriginTPC, "DIGITS"}, Lifetime::Timeframe},
        {"check", ConcreteDataTypeMatcher{gDataOriginTPC, "DIGITSMCTR"}, Lifetime::Timeframe},
      };
      for (auto const& inputRef : InputRecordWalker(pc.inputs())) {
        auto const* sectorHeader = DataRefUtils::getHeader<o2::tpc::TPCSectorHeader*>(inputRef);
        if (sectorHeader == nullptr) {
          LOG(error) << "sector header missing on header stack for input on " << inputRef.spec->binding;
          continue;
        }
        const int sector = sectorHeader->sector();
        if (DataRefUtils::match(inputRef, {"check", ConcreteDataTypeMatcher{gDataOriginTPC, "DIGITS"}})) {
          inputs[sector].dataref = inputRef;
        }
        if (DataRefUtils::match(inputRef, {"check", ConcreteDataTypeMatcher{gDataOriginTPC, "DIGITSMCTR"}})) {
          inputs[sector].mclabelref = inputRef;
        }
      }
      for (auto const& input : inputs) {
        if (processAttributes->sendMC && !DataRefUtils::isValid(input.second.mclabelref)) {
          throw std::runtime_error("missing the required MC label data for sector " + std::to_string(input.first));
        }
        processSectorFunction(pc, input.second.dataref, input.second.mclabelref);
      }
    };
    return processingFct;
  };

  auto createInputSpecs = [](bool makeMcInput) {
    std::vector<InputSpec> inputSpecs{
      InputSpec{"digits", gDataOriginTPC, "DIGITS", 0, Lifetime::Timeframe},
    };
    if (makeMcInput) {
      constexpr o2::header::DataDescription datadesc("DIGITSMCTR");
      inputSpecs.emplace_back("mclabels", gDataOriginTPC, datadesc, 0, Lifetime::Timeframe);
    }
    return std::move(inputSpecs);
  };

  auto createOutputSpecs = [](bool makeMcOutput) {
    std::vector<OutputSpec> outputSpecs{
      OutputSpec{{"clusters"}, gDataOriginTPC, "CLUSTERHW", 0, Lifetime::Timeframe},
    };
    if (makeMcOutput) {
      OutputLabel label{"clusterlbl"};
      // FIXME: define common data type specifiers
      constexpr o2::header::DataDescription datadesc("CLUSTERHWMCLBL");
      outputSpecs.emplace_back(label, gDataOriginTPC, datadesc, 0, Lifetime::Timeframe);
    }
    return std::move(outputSpecs);
  };

  return DataProcessorSpec{processorName,
                           {createInputSpecs(sendMC)},
                           {createOutputSpecs(sendMC)},
                           AlgorithmSpec(initFunction)};
}

} // namespace tpc
} // namespace o2
