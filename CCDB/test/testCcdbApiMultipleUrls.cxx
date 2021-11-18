#define BOOST_TEST_MODULE CCDB
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "CCDB/CcdbApi.h"
#include "CCDB/IdPath.h"    // just as test object
#include "CommonUtils/RootChain.h" // just as test object
#include "CCDB/CCDBTimeStampUtils.h"
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <cstdio>
#include <cassert>
#include <iostream>
#include <cstdio>
#include <curl/curl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <TH1F.h>
#include <chrono>
#include <CommonUtils/StringUtils.h>
#include <TMessage.h>
#include <TStreamerInfo.h>
#include <TGraph.h>
#include <TTree.h>
#include <TString.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include <boost/optional/optional.hpp>

using namespace std;
using namespace o2::ccdb;
namespace utf = boost::unit_test;
namespace tt = boost::test_tools;

static string ccdbUrl;
static string basePath;
bool hostReachable = false;

/**
 * Global fixture, ie general setup and teardown
 */
struct Fixture {
  Fixture()
  {
    CcdbApi api;
    ccdbUrl = "https://localhost:22,https://localhost:8080,http://ccdb-test.cern.ch:8080";
    api.init(ccdbUrl);
    cout << "ccdb url: " << ccdbUrl << endl;
    hostReachable = api.isHostReachable();
    cout << "Is host reachable ? --> " << hostReachable << endl;
    basePath = string("Test/pid") + getpid() + "/";
    cout << "Path we will use in this test suite : " + basePath << endl;
  }
  ~Fixture()
  {
    if (hostReachable) {
      CcdbApi api;
      map<string, string> metadata;
      api.init(ccdbUrl);
      api.truncate(basePath + "*");
      cout << "Test data truncated (" << basePath << ")" << endl;
    }
  }
};
BOOST_GLOBAL_FIXTURE(Fixture);

/**
 * Just an accessor to the hostReachable variable to be used to determine whether tests can be ran or not.
 */
struct if_reachable {
  tt::assertion_result operator()(utf::test_unit_id)
  {
    return hostReachable;
  }
};

/**
 * Fixture for the tests, i.e. code is ran in every test that uses it, i.e. it is like a setup and teardown for tests.
 */
struct test_fixture {
  test_fixture()
  {
    api.init(ccdbUrl);
    metadata["Hello"] = "World";
    std::cout << "*** " << boost::unit_test::framework::current_test_case().p_name << " ***" << std::endl;
  }
  ~test_fixture() = default;

  CcdbApi api;
  map<string, string> metadata;
};

BOOST_AUTO_TEST_CASE(storeAndRetrieve, *utf::precondition(if_reachable()))
{
  test_fixture f;

  TH1F h1("th1name", "th1name", 100, 0, 99);
  h1.FillRandom("gaus", 10000);
  BOOST_CHECK_EQUAL(h1.ClassName(), "TH1F");
  cout << "ccdb/TObject/TEST" << endl;
  long timestamp = getCurrentTimestamp();
  f.api.storeAsTFile(&h1, "ccdb/TObject/TEST", f.metadata, timestamp);

  TObject* returnResult = f.api.retrieve("ccdb/TObject/TEST/" + to_string(timestamp), f.metadata, timestamp);
  BOOST_CHECK(returnResult != nullptr);
}
