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

#define BOOST_TEST_MODULE CCDB
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "testCCDBResponseResources.h"
#include <CCDB/CCDBResponse.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace o2::ccdb;

CCDBResponse parse(const char* reply)
{
  std::string* responseAsStr = new std::string(reply);
  CCDBResponse ccdbResponse(*responseAsStr);
  free(responseAsStr);
  return ccdbResponse;
}

BOOST_AUTO_TEST_CASE(TestCCDBResponseFullResponse)
{
  auto response = parse(fullResponse);

  std::cout << "Objects: " << response.getObjects().size() << " ";
  BOOST_CHECK(response.getObjects().size() == 3);
  if (response.getObjects().size() == 3) {
    BOOST_CHECK(response.getObjects()[0].getProperty("id") == "407f3a65-4c7b-11ec-8cf8-200114580202");
    BOOST_CHECK(response.getObjects()[1].getProperty("id") == "e5183d1a-4c7a-11ec-9d71-7f000001aa8b");
    BOOST_CHECK(response.getObjects()[2].getProperty("id") == "52d3f61a-4c6b-11ec-a98e-7f000001aa8b");
  }

  std::cout << "Subfolders: " << response.getSubFolders().size() << " ";
  BOOST_CHECK(response.getSubFolders().size() == 1);
  if (response.getSubFolders().size() == 1) {
    BOOST_CHECK(response.getSubFolders()[0] == "Users/g/grigoras/testing/grid");
  }
}

BOOST_AUTO_TEST_CASE(TestCCDBResponseEmptyResponse)
{
  auto response = parse(emptyResponse);

  std::cout << "Objects: " << response.getObjects().size() << " ";
  BOOST_CHECK(response.getObjects().size() == 0);

  std::cout << "Subfolders: " << response.getSubFolders().size() << " ";
  BOOST_CHECK(response.getSubFolders().size() == 0);
}