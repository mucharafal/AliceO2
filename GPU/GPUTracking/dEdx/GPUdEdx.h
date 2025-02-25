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

/// \file GPUdEdx.h
/// \author David Rohr

#ifndef GPUDEDX_H
#define GPUDEDX_H

#include "GPUDef.h"
#include "GPUTPCGeometry.h"
#include "GPUCommonMath.h"
#include "GPUParam.h"
#include "GPUdEdxInfo.h"
#if defined(GPUCA_HAVE_O2HEADERS) && !defined(GPUCA_OPENCL1)
#include "DataFormatsTPC/Defs.h"
#include "DataFormatsTPC/CalibdEdxContainer.h"
#endif

namespace GPUCA_NAMESPACE
{
namespace gpu
{
#if !defined(GPUCA_HAVE_O2HEADERS) || defined(GPUCA_OPENCL1)

class GPUdEdx
{
 public:
  GPUd() void clear() {}
  GPUd() void fillCluster(float qtot, float qmax, int padRow, unsigned char slice, float trackSnp, float trackTgl, const GPUParam& param, const GPUCalibObjectsConst& calib, float z, float relPad, float relTime) {}
  GPUd() void fillSubThreshold(int padRow, const GPUParam& param) {}
  GPUd() void computedEdx(GPUdEdxInfo& output, const GPUParam& param) {}
};

#else

class GPUdEdx
{
 public:
  // The driver must call clear(), fill clusters row by row outside-in, then run computedEdx() to get the result
  GPUd() void clear();
  GPUd() void fillCluster(float qtot, float qmax, int padRow, unsigned char slice, float trackSnp, float trackTgl, const GPUParam& param, const GPUCalibObjectsConst& calib, float z, float relPad, float relTime);
  GPUd() void fillSubThreshold(int padRow, const GPUParam& param);
  GPUd() void computedEdx(GPUdEdxInfo& output, const GPUParam& param);

 private:
  GPUd() float GetSortTruncMean(GPUCA_DEDX_STORAGE_TYPE* array, int count, int trunclow, int trunchigh);
  GPUd() void checkSubThresh(int roc);

  template <typename T, typename fake = void>
  struct scalingFactor;
  template <typename fake>
  struct scalingFactor<unsigned short, fake> {
    static constexpr float factor = 4.f;
    static constexpr float round = 0.5f;
  };
  template <typename fake>
  struct scalingFactor<float, fake> {
    static constexpr float factor = 1.f;
    static constexpr float round = 0.f;
  };
#if defined(__CUDACC__) || defined(__HIPCC__)
  template <typename fake>
  struct scalingFactor<half, fake> {
    static constexpr float factor = 1.f;
    static constexpr float round = 0.f;
  };
#endif

  static constexpr int MAX_NCL = GPUCA_ROW_COUNT; // Must fit in mNClsROC (unsigned char)!

  GPUCA_DEDX_STORAGE_TYPE mChargeTot[MAX_NCL]; // No need for default, just some memory
  GPUCA_DEDX_STORAGE_TYPE mChargeMax[MAX_NCL]; // No need for default, just some memory
  float mSubThreshMinTot = 0.f;
  float mSubThreshMinMax = 0.f;
  unsigned char mNClsROC[4] = {0};
  unsigned char mNClsROCSubThresh[4] = {0};
  unsigned char mCount = 0;
  unsigned char mLastROC = 255;
  char mNSubThresh = 0;
};

GPUdi() void GPUdEdx::checkSubThresh(int roc)
{
  if (roc != mLastROC) {
    if (mNSubThresh && mCount + mNSubThresh <= MAX_NCL) {
      for (int i = 0; i < mNSubThresh; i++) {
        mChargeTot[mCount] = (GPUCA_DEDX_STORAGE_TYPE)(mSubThreshMinTot * scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::factor + scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::round);
        mChargeMax[mCount++] = (GPUCA_DEDX_STORAGE_TYPE)(mSubThreshMinMax * scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::factor + scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::round);
      }
      mNClsROC[mLastROC] += mNSubThresh;
      mNClsROCSubThresh[mLastROC] += mNSubThresh;
    }
    mNSubThresh = 0;
    mSubThreshMinTot = 1e10f;
    mSubThreshMinMax = 1e10f;
  }

  mLastROC = roc;
}

GPUdnii() void GPUdEdx::fillCluster(float qtot, float qmax, int padRow, unsigned char slice, float trackSnp, float trackTgl, const GPUParam& GPUrestrict() param, const GPUCalibObjectsConst& calib, float z, float relPad, float relTime)
{
  if (mCount >= MAX_NCL) {
    return;
  }

  // container containing all the dE/dx corrections
  auto calibContainer = calib.dEdxCalibContainer;

  const int roc = param.tpcGeometry.GetROC(padRow);
  checkSubThresh(roc);
  float snp2 = trackSnp * trackSnp;
  if (snp2 > GPUCA_MAX_SIN_PHI_LOW) {
    snp2 = GPUCA_MAX_SIN_PHI_LOW;
  }

  // setting maximum for snp for which the calibration object was created
  const float maxSnp = calibContainer->getMaxSinPhiTopologyCorrection();
  float snp = CAMath::Abs(trackSnp);
  if (snp > maxSnp) {
    snp = maxSnp;
  }

  // tanTheta local dip angle: z angle - dz/dx (cm/cm)
  const float sec2 = 1.f / (1.f - snp2);
  const float tgl2 = trackTgl * trackTgl;
  float tanTheta = CAMath::Sqrt(tgl2 * sec2);
  const float maxTanTheta = calibContainer->getMaxTanThetaTopologyCorrection();
  if (tanTheta > maxTanTheta) {
    tanTheta = maxTanTheta;
  }

  // getting the topology correction
  const float absRelPad = CAMath::Abs(relPad);
  const int region = param.tpcGeometry.GetRegion(padRow);
  z = CAMath::Abs(z);
  const float qMaxTopologyCorr = calibContainer->getTopologyCorrection(region, o2::tpc::ChargeType::Max, tanTheta, snp, z, absRelPad, relTime);
  const float qTotTopologyCorr = calibContainer->getTopologyCorrection(region, o2::tpc::ChargeType::Tot, tanTheta, snp, z, absRelPad, relTime);

  qmax /= qMaxTopologyCorr;
  qtot /= qTotTopologyCorr;

  tpc::StackID stack{
    slice,
    static_cast<tpc::GEMstack>(roc)};

  // getting the residual dE/dx correction
  const float qMaxResidualCorr = calibContainer->getResidualCorrection(stack, tpc::ChargeType::Max, z, trackTgl);
  const float qTotResidualCorr = calibContainer->getResidualCorrection(stack, tpc::ChargeType::Tot, z, trackTgl);
  qmax /= qMaxResidualCorr;
  qtot /= qTotResidualCorr;

  mChargeTot[mCount] = (GPUCA_DEDX_STORAGE_TYPE)(qtot * scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::factor + scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::round);
  mChargeMax[mCount++] = (GPUCA_DEDX_STORAGE_TYPE)(qmax * scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::factor + scalingFactor<GPUCA_DEDX_STORAGE_TYPE>::round);
  mNClsROC[roc]++;
  if (qtot < mSubThreshMinTot) {
    mSubThreshMinTot = qtot;
  }
  if (qmax < mSubThreshMinMax) {
    mSubThreshMinMax = qmax;
  }
}

GPUdi() void GPUdEdx::fillSubThreshold(int padRow, const GPUParam& GPUrestrict() param)
{
  const int roc = param.tpcGeometry.GetROC(padRow);
  checkSubThresh(roc);
  mNSubThresh++;
}

#endif // !GPUCA_HAVE_O2HEADERS || __OPENCL1__
} // namespace gpu
} // namespace GPUCA_NAMESPACE

#endif
