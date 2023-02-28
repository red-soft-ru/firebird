#include "firebird.h"
#include "boost/test/unit_test.hpp"

#include "../jrd/cvt_proto.h"

using namespace Firebird;
using namespace Jrd;

BOOST_AUTO_TEST_SUITE(EngineSuite)
BOOST_AUTO_TEST_SUITE(DscConvertTests)


void toDoubleTest(const string& input, double expectedValue, SSHORT expectedScale)
{
	double outputValue;
	SSHORT outputScale = 0;
	SSHORT type = CVT_get_numeric((const UCHAR*)input.c_str(), input.length(), &outputScale, (void*)(&outputValue));
	BOOST_TEST(type == dtype_double);
	BOOST_TEST(outputValue == expectedValue);
	BOOST_TEST(outputScale == expectedScale);
}

BOOST_AUTO_TEST_CASE(TextToDouble)
{
	toDoubleTest("345.12e-2", 3.4512, 4);
	toDoubleTest("34512e-4", 3.4512, 4);
	toDoubleTest("34.512e-1", 3.4512, 4);
	toDoubleTest("3.4512e0", 3.4512, 4);
	toDoubleTest("0.34512e1", 3.4512, 4);
	toDoubleTest("0.034512e2", 3.4512, 4);
	toDoubleTest("0.0034512e3", 3.4512, 4);
}

BOOST_AUTO_TEST_SUITE_END()	// DscConvertTests
BOOST_AUTO_TEST_SUITE_END()	// EngineSuite
