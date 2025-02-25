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

#include "Framework/RootSerializationSupport.h"
#include "DataSampling/DataSampling.h"
#include <thread>

using namespace o2::framework;
using namespace o2::utilities;

void customize(std::vector<CompletionPolicy>& policies)
{
  DataSampling::CustomizeInfrastructure(policies);
}
void customize(std::vector<ChannelConfigurationPolicy>& policies)
{
  DataSampling::CustomizeInfrastructure(policies);
}

#include "Framework/InputSpec.h"
#include "Framework/DataProcessorSpec.h"
#include "Framework/runDataProcessing.h"
#include "Framework/TMessageSerializer.h"
#include "DataSampling/DataSamplingHeader.h"
#include "Framework/Logger.h"
#include "Framework/DataRefUtils.h"
#include <TClonesArray.h>
#include <TObjString.h>
#include <TH1F.h>
#include <TString.h>

#include <boost/algorithm/string.hpp>

#include <chrono>
#include <iostream>

struct FakeCluster {
  float x;
  float y;
  float z;
  float q;
};
using DataHeader = o2::header::DataHeader;

size_t collectionChunkSize = 1000;
void someDataProducerAlgorithm(ProcessingContext& ctx);
void someProcessingStageAlgorithm(ProcessingContext& ctx);
void someSinkAlgorithm(ProcessingContext& ctx);

WorkflowSpec defineDataProcessing(ConfigContext const&)
{
  DataProcessorSpec podDataProducer{
    "podDataProducer",
    Inputs{},
    {OutputSpec{{"TPC", "CLUSTERS"}},
     OutputSpec{{"ITS", "CLUSTERS"}}},
    AlgorithmSpec{
      (AlgorithmSpec::ProcessCallback)someDataProducerAlgorithm}};

  DataProcessorSpec processingStage{
    "processingStage",
    Inputs{
      {"dataTPC", {"TPC", "CLUSTERS"}},
      {"dataITS", {"ITS", "CLUSTERS"}}},
    Outputs{
      {{"TPC", "CLUSTERS_P"}},
      {{"ITS", "CLUSTERS_P"}}},
    AlgorithmSpec{
      (AlgorithmSpec::ProcessCallback)someProcessingStageAlgorithm}};

  DataProcessorSpec podSink{
    "podSink",
    Inputs{
      {"dataTPC-proc", {"TPC", "CLUSTERS_P"}},
      {"dataITS-proc", {"ITS", "CLUSTERS_P"}}},
    Outputs{},
    AlgorithmSpec{
      (AlgorithmSpec::ProcessCallback)someSinkAlgorithm}};

  // clang-format off
  DataProcessorSpec qcTaskTpc{
    "qcTaskTpc",
    Inputs{
      { "TPC_CLUSTERS_S",   {"DS", "simpleQcTask0"}},
      { "TPC_CLUSTERS_P_S", {"DS", "simpleQcTask1"}}
    },
    Outputs{},
    AlgorithmSpec{
      (AlgorithmSpec::ProcessCallback)[](ProcessingContext& ctx) {
        auto inputDataTpc = reinterpret_cast<const FakeCluster*>(ctx.inputs().get("TPC_CLUSTERS_S").payload);
        auto inputDataTpcProcessed = reinterpret_cast<const FakeCluster*>(ctx.inputs().get(
          "TPC_CLUSTERS_P_S").payload);

        auto ref = ctx.inputs().get("TPC_CLUSTERS_S");
        const auto* dataHeader = DataRefUtils::getHeader<DataHeader*>(ref);

        bool dataGood = true;
        for (int j = 0; j < dataHeader->payloadSize / sizeof(FakeCluster); ++j) {
          float diff = std::abs(-inputDataTpc[j].x - inputDataTpcProcessed[j].x) +
                       std::abs(2 * inputDataTpc[j].y - inputDataTpcProcessed[j].y) +
                       std::abs(inputDataTpc[j].z * inputDataTpc[j].q - inputDataTpcProcessed[j].z) +
                       std::abs(inputDataTpc[j].q - inputDataTpcProcessed[j].q);
          if (diff > 1) {
            dataGood = false;
            break;
          }
        }

        LOG(info) << "qcTaskTPC - received data is " << (dataGood ? "correct" : "wrong");

        const auto* dsHeader = DataRefUtils::getHeader<DataSamplingHeader*>(ref);
        if (dsHeader) {
          LOG(info) << "Matching messages seen by Dispatcher: " << dsHeader->totalEvaluatedMessages
                    << ", accepted: " << dsHeader->totalAcceptedMessages
                    << ", sample time: " << dsHeader->sampleTimeUs
                    << ", device ID: " << dsHeader->deviceID.str;
        } else {
          LOG(error) << "DataSamplingHeader missing!";
        }

      }
    }
  };

  DataProcessorSpec rootDataProducer{
    "rootDataProducer",
    {},
    {
      OutputSpec{ "TST", "HISTOS", 0, Lifetime::Timeframe },
      OutputSpec{ "TST", "STRING", 0, Lifetime::Timeframe }
    },
    AlgorithmSpec{
      [](ProcessingContext& ctx) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // Create an histogram
        auto& singleHisto = ctx.outputs().make<TH1F>(Output{ "TST", "HISTOS", 0 }, "h1", "test", 100, -10., 10.);
        auto& aString = ctx.outputs().make<TObjString>(Output{ "TST", "STRING", 0 }, "foo");
        singleHisto.FillRandom("gaus", 1000);
        Double_t stats[4];
        singleHisto.GetStats(stats);
        LOG(info) << "sumw" << stats[0] << "\n"
                  << "sumw2" << stats[1] << "\n"
                  << "sumwx" << stats[2] << "\n"
                  << "sumwx2" << stats[3] << "\n";
      }
    }
  };

  DataProcessorSpec rootSink{
    "rootSink",
    {
      InputSpec{ "histos", "TST", "HISTOS", 0, Lifetime::Timeframe },
      InputSpec{ "string", "TST", "STRING", 0, Lifetime::Timeframe },
    },
    {},
    AlgorithmSpec{
      [](ProcessingContext& ctx) {
        auto h = ctx.inputs().get<TH1F*>("histos");
        if (h.get() == nullptr) {
          throw std::runtime_error("Missing output");
        }
        Double_t stats[4];
        h->GetStats(stats);
        LOG(info) << "sumw" << stats[0] << "\n"
                  << "sumw2" << stats[1] << "\n"
                  << "sumwx" << stats[2] << "\n"
                  << "sumwx2" << stats[3] << "\n";
        auto s = ctx.inputs().get<TObjString*>("string");

        LOG(info) << "String is " << s->GetString().Data();
      } }
  };

  DataProcessorSpec rootQcTask{
    "rootQcTask",
    {
      InputSpec{ "TST_HISTOS_S", {"DS", "rootQcTask0"}},
      InputSpec{ "TST_STRING_S", {"DS", "rootQcTask1"}},
    },
    Outputs{},
    AlgorithmSpec{
      [](ProcessingContext& ctx) {
        auto h = ctx.inputs().get<TH1F*>("TST_HISTOS_S");
        if (h.get() == nullptr) {
          throw std::runtime_error("Missing TST_HISTOS_S");
        }
        Double_t stats[4];
        h->GetStats(stats);
        LOG(info) << "sumw" << stats[0] << "\n"
                  << "sumw2" << stats[1] << "\n"
                  << "sumwx" << stats[2] << "\n"
                  << "sumwx2" << stats[3] << "\n";
        auto s = ctx.inputs().get<TObjString*>("TST_STRING_S");

        LOG(info) << "qcTaskTst: TObjString is " << (std::string("foo") == s->GetString().Data() ? "correct" : "wrong");
      }
    }
  };

  WorkflowSpec specs{
    podDataProducer,
    processingStage,
    podSink,
    qcTaskTpc,

    rootDataProducer,
    rootSink,
    rootQcTask
  };

  const char* o2Root = getenv("O2_ROOT");
  if (o2Root == nullptr) {
    throw std::runtime_error("The O2_ROOT environment variable is not set, probably the O2 environment has not been loaded.");
  }
  std::string configurationSource = std::string("json:/") + o2Root + "/share/etc/exampleDataSamplingConfig.json";
  LOG(info) << "Using config source: " << configurationSource;
  DataSampling::GenerateInfrastructure(specs, configurationSource, 1);
  return specs;
}
// clang-format on

void someDataProducerAlgorithm(ProcessingContext& ctx)
{
  std::this_thread::sleep_for(std::chrono::seconds(1));
  // Creates a new message of size collectionChunkSize which
  // has "TPC" as data origin and "CLUSTERS" as data description.
  auto& tpcClusters = ctx.outputs().make<FakeCluster>(Output{"TPC", "CLUSTERS", 0}, collectionChunkSize);
  int i = 0;

  for (auto& cluster : tpcClusters) {
    assert(i < collectionChunkSize);
    cluster.x = i;
    cluster.y = i;
    cluster.z = i;
    cluster.q = rand() % 1000;
    i++;
  }

  auto& itsClusters = ctx.outputs().make<FakeCluster>(Output{"ITS", "CLUSTERS", 0}, collectionChunkSize);
  i = 0;
  for (auto& cluster : itsClusters) {
    assert(i < collectionChunkSize);
    cluster.x = i;
    cluster.y = i;
    cluster.z = i;
    cluster.q = rand() % 10;
    i++;
  }
}

void someProcessingStageAlgorithm(ProcessingContext& ctx)
{
  const FakeCluster* inputDataTpc = reinterpret_cast<const FakeCluster*>(ctx.inputs().get("dataTPC").payload);
  const FakeCluster* inputDataIts = reinterpret_cast<const FakeCluster*>(ctx.inputs().get("dataITS").payload);

  auto processedTpcClusters =
    ctx.outputs().make<FakeCluster>(Output{"TPC", "CLUSTERS_P", 0}, collectionChunkSize);
  auto processedItsClusters =
    ctx.outputs().make<FakeCluster>(Output{"ITS", "CLUSTERS_P", 0}, collectionChunkSize);

  int i = 0;
  for (auto& cluster : processedTpcClusters) {
    assert(i < collectionChunkSize);
    cluster.x = -inputDataTpc[i].x;
    cluster.y = 2 * inputDataTpc[i].y;
    cluster.z = inputDataTpc[i].z * inputDataTpc[i].q;
    cluster.q = inputDataTpc[i].q;
    i++;
  }

  i = 0;
  for (auto& cluster : processedItsClusters) {
    assert(i < collectionChunkSize);
    cluster.x = -inputDataIts[i].x;
    cluster.y = 2 * inputDataIts[i].y;
    cluster.z = inputDataIts[i].z * inputDataIts[i].q;
    cluster.q = inputDataIts[i].q;
    i++;
  }
};

void someSinkAlgorithm(ProcessingContext& ctx)
{
  const FakeCluster* inputDataTpc = reinterpret_cast<const FakeCluster*>(ctx.inputs().get("dataTPC-proc").payload);
  const FakeCluster* inputDataIts = reinterpret_cast<const FakeCluster*>(ctx.inputs().get("dataITS-proc").payload);
}
