// Copyright 2019-2021 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef CCDB_RESPONSE_H_
#define CCDB_RESPONSE_H_

#include <Rtypes.h>
#include <map>
#include "rapidjson/document.h"

namespace o2
{
namespace ccdb
{

class CCDBObjectDescription
{
 public:
  CCDBObjectDescription() = default;
  CCDBObjectDescription(rapidjson::Value::ConstValueIterator jsonObject);
  ~CCDBObjectDescription() = default;

  std::string getProperty(const std::string &propertyName);
  // std::string toString();

  std::map<std::string, std::string> stringValues{};
  std::map<std::string, int64_t> intValues{};
  std::map<std::string, double> doubleValues{};
  std::map<std::string, bool> booleanValues{};

 private:

  ClassDefNV(CCDBObjectDescription, 1);
};

class CCDBResponse
{
 public:
  CCDBResponse() = default;
  CCDBResponse(const std::string &json);
  CCDBResponse(std::vector<CCDBObjectDescription> _objects, std::vector<std::string> _subfolders)
    : objects(_objects), subfolders(_subfolders){};
  ~CCDBResponse() = default;

  std::vector<std::string> getSubFolders();

  std::vector<CCDBObjectDescription> getObjects();

  // std::string toString();

 private:
  std::vector<CCDBObjectDescription> objects{};
  std::vector<std::string> subfolders{};

  static std::pair<std::string, std::string> splitResponseOnObjectsAndSubFolders(const std::string &response);
  static std::vector<std::string> matchObjects(const std::string &objectsListAsString);
  static std::vector<CCDBObjectDescription> parseObjects(const std::string &objectsListAsString);
  static std::vector<std::string> parseSubfolders(const std::string &subfoldersAsString);
  static std::string sanitizeObjectName(const std::string& objectName);

  ClassDefNV(CCDBResponse, 1);
};

} // namespace ccdb
} // namespace o2

#endif
