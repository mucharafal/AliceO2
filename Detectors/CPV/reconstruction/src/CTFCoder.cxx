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

/// \file   CTFCoder.cxx
/// \author ruben.shahoyan@cern.ch
/// \brief class for entropy encoding/decoding of CPV data

#include "CPVReconstruction/CTFCoder.h"
#include "CommonUtils/StringUtils.h"
#include <TTree.h>

using namespace o2::cpv;

///___________________________________________________________________________________
// Register encoded data in the tree (Fill is not called, will be done by caller)
void CTFCoder::appendToTree(TTree& tree, CTF& ec)
{
  ec.appendToTree(tree, mDet.getName());
}

///___________________________________________________________________________________
// extract and decode data from the tree
void CTFCoder::readFromTree(TTree& tree, int entry, std::vector<TriggerRecord>& trigVec, std::vector<Cluster>& cluVec)
{
  assert(entry >= 0 && entry < tree.GetEntries());
  CTF ec;
  ec.readFromTree(tree, mDet.getName(), entry);
  decode(ec, trigVec, cluVec);
}

///________________________________
void CTFCoder::createCoders(const std::vector<char>& bufVec, o2::ctf::CTFCoderBase::OpType op)
{
  const auto ctf = CTF::getImage(bufVec.data());
  // just to get types
  uint16_t bcInc = 0, entries = 0, cluPosX = 0, cluPosZ = 0;
  uint32_t orbitInc = 0;
  uint8_t energy = 0, status = 0;
#define MAKECODER(part, slot) createCoder<decltype(part)>(op, ctf.getFrequencyTable(slot), ctf.getMetadata(slot).probabilityBits, int(slot))
  // clang-format off
  MAKECODER(bcInc,      CTF::BLC_bcIncTrig);
  MAKECODER(orbitInc,   CTF::BLC_orbitIncTrig);
  MAKECODER(entries,    CTF::BLC_entriesTrig);
  MAKECODER(cluPosX,    CTF::BLC_posX);
  MAKECODER(cluPosZ,    CTF::BLC_posZ);
  MAKECODER(energy,     CTF::BLC_energy);
  MAKECODER(status,     CTF::BLC_status);
  // clang-format on
}
