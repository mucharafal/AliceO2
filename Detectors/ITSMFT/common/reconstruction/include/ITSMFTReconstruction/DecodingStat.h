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

/// \file DecodingStat.h
/// \brief Alpide Chip and GBT link decoding statistics

#ifndef _ALICEO2_DECODINGSTAT_H_
#define _ALICEO2_DECODINGSTAT_H_

#include <string>
#include <array>
#include <Rtypes.h>
#include "ITSMFTReconstruction/GBTWord.h"

namespace o2
{
namespace itsmft
{
class ChipPixelData;

struct ChipStat {

  enum DecErrors : int {
    BusyViolation,
    DataOverrun,
    Fatal,
    BusyOn,
    BusyOff,
    TruncatedChipEmpty,           // Data was truncated after ChipEmpty
    TruncatedChipHeader,          // Data was truncated after ChipHeader
    TruncatedRegion,              // Data was truncated after Region record
    TruncatedLondData,            // Data was truncated in the LongData record
    WrongDataLongPattern,         // LongData pattern has highest bit set
    NoDataFound,                  // Region is not followed by Short or Long data
    UnknownWord,                  // Unknown word was seen
    RepeatingPixel,               // Same pixel fired more than once
    WrongRow,                     // Non-existing row decoded
    APE_STRIP_START,              // lane entering strip data mode | See https://alice.its.cern.ch/jira/browse/O2-1717
    APE_STRIP_STOP,               // lane exiting strip data mode
    APE_DET_TIMEOUT,              // detector timeout (FATAL)
    APE_OOT_START,                // 8b10b OOT (FATAL, start)
    APE_PROTOCOL_ERROR,           // event protocol error marker (FATAL, start)
    APE_LANE_FIFO_OVERFLOW_ERROR, // lane FIFO overflow error (FATAL)
    APE_FSM_ERROR,                // FSM error (FATAL, SEU error, reached an unknown state)
    APE_OCCUPANCY_RATE_LIMIT,     // pending detector events limit (FATAL)
    APE_OCCUPANCY_RATE_LIMIT_2,   // pending detector events limit in packager(FATAL)
    NErrorsDefined
  };

  static constexpr std::array<std::string_view, NErrorsDefined> ErrNames = {
    "BusyViolation flag ON",                        // BusyViolation
    "DataOverrun flag ON",                          // DataOverrun
    "Fatal flag ON",                                // Fatal
    "BusyON",                                       // BusyOn
    "BusyOFF",                                      // BusyOff
    "Data truncated after ChipEmpty",               // TruncatedChipEmpty
    "Data truncated after ChipHeader",              // TruncatedChipHeader
    "Data truncated after Region",                  // TruncatedRegion
    "Data truncated after LongData",                // TruncatedLondData
    "LongData pattern has highest bit set",         // WrongDataLongPattern
    "Region is not followed by Short or Long data", // NoDataFound
    "Unknown word",                                 // UnknownWord
    "Same pixel fired multiple times",              // RepeatingPixel
    "Non-existing row decoded",                     // WrongRow
    "APE_STRIP_START",
    "APE_STRIP_STOP",
    "APE_DET_TIMEOUT",
    "APE_OOT_START",
    "APE_PROTOCOL_ERROR",
    "APE_LANE_FIFO_OVERFLOW_ERROR",
    "APE_FSM_ERROR",
    "APE_OCCUPANCY_RATE_LIMIT",
    "APE_OCCUPANCY_RATE_LIMIT_2"};

  uint16_t feeID = -1;
  size_t nHits = 0;
  std::array<uint32_t, NErrorsDefined> errorCounts = {};
  ChipStat() = default;
  ChipStat(uint16_t _feeID) : feeID(_feeID) {}

  void clear()
  {
    memset(errorCounts.data(), 0, sizeof(uint32_t) * errorCounts.size());
    nHits = 0;
  }
  // return APE DecErrors code or -1 if not APE error, set fatal flag if needd
  static int getAPECode(uint8_t c, bool& ft)
  {
    if (c < 0xf2 || c > 0xfa) {
      ft = false;
      return -1;
    }
    ft = c >= 0xf4;
    return APE_STRIP_START + c - 0xf2;
  }
  uint32_t getNErrors() const;
  void addErrors(uint32_t mask, uint16_t chID, int verbosity);
  void addErrors(const ChipPixelData& d, int verbosity);
  void print(bool skipNoErr = true, const std::string& pref = "FEEID") const;

  ClassDefNV(ChipStat, 1);
};

/// Statistics for per-link decoding
struct GBTLinkDecodingStat {
  /// counters for format checks

  enum DecErrors : int {
    ErrNoRDHAtStart,             // page does not start with RDH
    ErrPageNotStopped,           // RDH is stopped, but the time is not matching the ~stop packet
    ErrStopPageNotEmpty,         // Page with RDH.stop is not empty
    ErrPageCounterDiscontinuity, // RDH page counters for the same RU/trigger are not continuous
    ErrRDHvsGBTHPageCnt,         // RDH and GBT header page counters are not consistent
    ErrMissingGBTTrigger,        // GBT trigger word was expected but not found
    ErrMissingGBTHeader,         // GBT payload header was expected but not found
    ErrMissingGBTTrailer,        // GBT payload trailer was expected but not found
    ErrNonZeroPageAfterStop,     // all lanes were stopped but the page counter in not 0
    ErrUnstoppedLanes,           // end of FEE data reached while not all lanes received stop
    ErrDataForStoppedLane,       // data was received for stopped lane
    ErrNoDataForActiveLane,      // no data was seen for lane (which was not in timeout)
    ErrIBChipLaneMismatch,       // chipID (on module) was different from the lane ID on the IB stave
    ErrCableDataHeadWrong,       // cable data does not start with chip header or empty chip
    ErrInvalidActiveLanes,       // active lanes pattern conflicts with expected for given RU type
    ErrPacketCounterJump,        // jump in RDH.packetCounter
    ErrPacketDoneMissing,        // packet done is missing in the trailer while CRU page is not over
    ErrMissingDiagnosticWord,    // missing diagnostic word after RDH with stop
    ErrGBTWordNotRecognized,     // GBT word not recognized
    ErrWrongeCableID,            // Invalid cable ID
    NErrorsDefined
  };
  static constexpr std::array<std::string_view, NErrorsDefined> ErrNames = {
    "Page data not start with expected RDH",                             // ErrNoRDHAtStart
    "RDH is stopped, but the time is not matching the ~stop packet",     // ErrPageNotStopped
    "Page with RDH.stop does not contain diagnostic word only",          // ErrStopPageNotEmpty
    "RDH page counters for the same RU/trigger are not continuous",      // ErrPageCounterDiscontinuity
    "RDH and GBT header page counters are not consistent",               // ErrRDHvsGBTHPageCnt
    "GBT trigger word was expected but not found",                       // ErrMissingGBTTrigger
    "GBT payload header was expected but not found",                     // ErrMissingGBTHeader
    "GBT payload trailer was expected but not found",                    // ErrMissingGBTTrailer
    "All lanes were stopped but the page counter in not 0",              // ErrNonZeroPageAfterStop
    "End of FEE data reached while not all lanes received stop",         // ErrUnstoppedLanes
    "Data was received for stopped lane",                                // ErrDataForStoppedLane
    "No data was seen for lane (which was not in timeout)",              // ErrNoDataForActiveLane
    "ChipID (on module) was different from the lane ID on the IB stave", // ErrIBChipLaneMismatch
    "Cable data does not start with chip header or empty chip",          // ErrCableDataHeadWrong
    "Active lanes pattern conflicts with expected for given RU type",    // ErrInvalidActiveLanes
    "Jump in RDH_packetCounter",                                         // ErrPacketCounterJump
    "Packet done is missing in the trailer while CRU page is not over",  // ErrPacketDoneMissing
    "Missing diagnostic GBT word after RDH with stop",                   // ErrMissingDiagnosticWord
    "GBT word not recognized",                                           // ErrGBTWordNotRecognized
    "Wrong cable ID"                                                     // ErrWrongeCableID
  };

  uint32_t ruLinkID = 0; // Link ID within RU

  // Note: packet here is meant as a group of CRU pages belonging to the same trigger
  uint32_t nPackets = 0;                                                        // total number of packets (RDH pages)
  uint32_t nTriggers = 0;                                                       // total number of triggers (ROFs)
  std::array<uint32_t, NErrorsDefined> errorCounts = {};                        // error counters
  std::array<uint32_t, GBTDataTrailer::MaxStateCombinations> packetStates = {}; // packet status from the trailer

  void clear()
  {
    nPackets = 0;
    nTriggers = 0;
    errorCounts.fill(0);
    packetStates.fill(0);
  }

  void print(bool skipNoErr = true) const;

  ClassDefNV(GBTLinkDecodingStat, 2);
};

} // namespace itsmft
} // namespace o2
#endif
