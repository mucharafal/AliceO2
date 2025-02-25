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

/// \file GRPObject.h
/// \brief Header of the General Run Parameters object
/// \author ruben.shahoyan@cern.ch

#ifndef ALICEO2_DATA_GRPOBJECT_H_
#define ALICEO2_DATA_GRPOBJECT_H_

#include <Rtypes.h>
#include <cstdint>
#include <ctime>
#include "CommonConstants/LHCConstants.h"
#include "CommonTypes/Units.h"
#include "DetectorsCommonDataFormats/DetID.h"

namespace o2
{
namespace parameters
{
/*
 * Collects parameters describing the run, like the beam, magnet settings
 * masks for participating and triggered detectors etc.
 */

class GRPObject
{
  using beamDirection = o2::constants::lhc::BeamDirection;
  using DetID = o2::detectors::DetID;

 public:
  using timePoint = uint64_t; // std::time_t;

  enum ROMode : int { ABSENT = 0,
                      PRESENT = 0x1,
                      CONTINUOUS = PRESENT + (0x1 << 1),
                      TRIGGERING = PRESENT + (0x1 << 2) };

  GRPObject() = default;
  ~GRPObject() = default;

  /// getters/setters for Start and Stop times according to logbook
  timePoint getTimeStart() const { return mTimeStart; }
  timePoint getTimeEnd() const { return mTimeEnd; }
  void setTimeStart(timePoint t) { mTimeStart = t; }
  void setTimeEnd(timePoint t) { mTimeEnd = t; }

  void setNHBFPerTF(uint32_t n) { mNHBFPerTF = n; }
  uint32_t getNHBFPerTF() const { return mNHBFPerTF; }

  void setFirstOrbit(uint32_t o) { mFirstOrbit = o; }
  uint32_t getFirstOrbit() const { return mFirstOrbit; }

  /// getters/setters for beams crossing angle (deviation from 0)
  o2::units::AngleRad_t getCrossingAngle() const { return mCrossingAngle; }
  void setCrossingAngle(o2::units::AngleRad_t v) { mCrossingAngle = v; }
  /// getters/setters for given beam A and Z info, encoded as A<<16+Z
  int getBeamZ(beamDirection beam) const { return mBeamAZ[static_cast<int>(beam)] & 0xffff; }
  int getBeamA(beamDirection beam) const { return mBeamAZ[static_cast<int>(beam)] >> 16; }
  float getBeamZ2A(beamDirection beam) const;
  void setBeamAZ(int a, int z, beamDirection beam) { mBeamAZ[static_cast<int>(beam)] = (a << 16) + z; }
  /// getters/setters for beam energy per charge and per nucleon
  void setBeamEnergyPerZ(float v) { mBeamEnergyPerZ = v; }
  float getBeamEnergyPerZ() const { return mBeamEnergyPerZ; }
  float getBeamEnergyPerNucleon(beamDirection beam) const { return mBeamEnergyPerZ * getBeamZ2A(beam); }
  /// calculate center of mass energy per nucleon collision
  float getSqrtS() const;

  /// getters/setters for magnets currents
  o2::units::Current_t getL3Current() const { return mL3Current; }
  o2::units::Current_t getDipoleCurrent() const { return mDipoleCurrent; }
  bool getFieldUniformity() const { return mUniformField; }
  void setL3Current(o2::units::Current_t v) { mL3Current = v; }
  void setDipoleCurrent(o2::units::Current_t v) { mDipoleCurrent = v; }
  void setFieldUniformity(bool v) { mUniformField = v; }
  int8_t getNominalL3Field();
  /// getter/setter for data taking period name
  const std::string& getDataPeriod() const { return mDataPeriod; }
  void setDataPeriod(const std::string v) { mDataPeriod = v; }
  /// getter/setter for LHC state in the beggining of run
  const std::string& getLHCState() const { return mLHCState; }
  void setLHCState(const std::string v) { mLHCState = v; }
  // getter/setter for run identifier
  void setRun(int r) { mRun = r; }
  int getRun() const { return mRun; }
  /// getter/setter for fill identifier
  void setFill(int f) { mFill = f; }
  int getFill() const { return mFill; }
  /// getter/setter for masks of detectors in the readout
  DetID::mask_t getDetsReadOut() const { return mDetsReadout; }
  void setDetsReadOut(DetID::mask_t mask) { mDetsReadout = mask; }
  /// getter/setter for masks of detectors with continuos readout
  DetID::mask_t getDetsContinuousReadOut() const { return mDetsContinuousRO; }
  void setDetsContinuousReadOut(DetID::mask_t mask) { mDetsContinuousRO = mask; }
  /// getter/setter for masks of detectors providing the trigger
  DetID::mask_t getDetsTrigger() const { return mDetsTrigger; }
  void setDetsTrigger(DetID::mask_t mask) { mDetsTrigger = mask; }
  /// add specific detector to the list of readout detectors
  void addDetReadOut(DetID id) { mDetsReadout |= id.getMask(); }
  /// remove specific detector from the list of readout detectors
  void remDetReadOut(DetID id)
  {
    mDetsReadout &= ~id.getMask();
    remDetContinuousReadOut(id);
    remDetTrigger(id);
  }
  /// add specific detector to the list of continuously readout detectors
  void addDetContinuousReadOut(DetID id) { mDetsContinuousRO |= id.getMask(); }
  /// remove specific detector from the list of continuouslt readout detectors
  void remDetContinuousReadOut(DetID id) { mDetsContinuousRO &= ~id.getMask(); }
  /// add specific detector to the list of triggering detectors
  void addDetTrigger(DetID id) { mDetsTrigger |= id.getMask(); }
  /// remove specific detector from the list of triggering detectors
  void remDetTrigger(DetID id) { mDetsTrigger &= ~id.getMask(); }
  /// test if detector is read out
  bool isDetReadOut(DetID id) const { return (mDetsReadout & id.getMask()) != 0; }
  /// test if detector is read out
  bool isDetContinuousReadOut(DetID id) const { return (mDetsContinuousRO & id.getMask()) != 0; }
  /// test if detector is triggering
  bool isDetTriggers(DetID id) const { return (mDetsTrigger & id.getMask()) != 0; }
  /// set detector readout mode status
  void setDetROMode(DetID id, ROMode status);
  ROMode getDetROMode(DetID id) const;

  /// extra selections
  /// mask of readout detectors with addition selections. "only" overrides "skip"
  DetID::mask_t getDetsReadOut(DetID::mask_t only, DetID::mask_t skip = 0) const { return only.any() ? (mDetsReadout & only) : (mDetsReadout ^ skip); }
  /// same with comma-separate list of detector names
  DetID::mask_t getDetsReadOut(const std::string& only, const std::string& skip = "") const { return getDetsReadOut(DetID::getMask(only), DetID::getMask(skip)); }

  /// print itself
  void print() const;

  static GRPObject* loadFrom(const std::string& grpFileName = "");

 private:
  timePoint mTimeStart = 0;      ///< DAQ_time_start entry from DAQ logbook
  timePoint mTimeEnd = LONG_MAX; ///< DAQ_time_end entry from DAQ logbook

  uint32_t mFirstOrbit = 0;  /// 1st orbit of the 1st TF, in the MC set at digitization // RS Not sure it will stay in GRP, may go to some CTP object
  uint32_t mNHBFPerTF = 256; /// Number of HBFrames per TF

  DetID::mask_t mDetsReadout;      ///< mask of detectors which are read out
  DetID::mask_t mDetsContinuousRO; ///< mask of detectors read out in continuos mode
  DetID::mask_t mDetsTrigger;      ///< mask of detectors which provide trigger

  o2::units::AngleRad_t mCrossingAngle = 0.f; ///< crossing angle in radians (as deviation from pi)
  o2::units::Current_t mL3Current = 0.f;      ///< signed current in L3
  o2::units::Current_t mDipoleCurrent = 0.f;  ///< signed current in Dipole
  bool mUniformField = false;                 ///< uniformity of magnetic field
  float mBeamEnergyPerZ = 0.f;                ///< beam energy per charge (i.e. sqrt(s)/2 for pp)

  int8_t mNominalL3Field = 0;        //!< Nominal L3 field deduced from mL3Current
  bool mNominalL3FieldValid = false; //!< Has the field been computed (for caching)

  int mBeamAZ[beamDirection::NBeamDirections] = {0, 0}; ///< A<<16+Z for each beam

  int mRun = 0;                 ///< run identifier
  int mFill = 0;                ///< fill identifier
  std::string mDataPeriod = ""; ///< name of the period
  std::string mLHCState = "";   ///< machine state

  ClassDefNV(GRPObject, 7);
};

//______________________________________________
inline float GRPObject::getBeamZ2A(beamDirection b) const
{
  // Z/A of beam 0 or 1
  int a = getBeamA(b);
  return a ? getBeamZ(b) / static_cast<float>(a) : 0.f;
}

//______________________________________________
inline int8_t GRPObject::getNominalL3Field()
{
  // compute nominal L3 field in kG

  if (mNominalL3FieldValid == false) {
    mNominalL3Field = std::lround(5.f * mL3Current / 30000.f);
    mNominalL3FieldValid = true;
  }
  return mNominalL3Field;
}

} // namespace parameters
} // namespace o2

#endif
