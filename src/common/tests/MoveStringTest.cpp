#include "boost/test/unit_test.hpp"
#include "../common/classes/fb_string.h"

#include <string_view>

using namespace Firebird;


BOOST_AUTO_TEST_SUITE(MoveSuite)
BOOST_AUTO_TEST_SUITE(StringMoveSemantics)

static constexpr std::string_view EmptyString = "";
static constexpr std::string_view TestString = "123";
static constexpr std::string_view TestBigString = "BiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiigString";
static_assert(TestBigString.length() > Firebird::string::INLINE_BUFFER_SIZE);

void checkMovedString(const string& moved)
{
	BOOST_CHECK(moved.c_str() != nullptr);
	BOOST_CHECK(moved.length() == 0);
	BOOST_CHECK(std::string_view(moved.data(), moved.length()) == EmptyString);
}

#define checkSameString(str) BOOST_CHECK(std::string_view(str.data(), str.length()) == TestString)

#define checkSameBigString(str) \
	BOOST_CHECK(std::string_view(str.data(), str.length()) == TestBigString)


BOOST_AUTO_TEST_SUITE(MoveString)


// Move only big strings in the same pool
BOOST_AUTO_TEST_CASE(DefaultPoolMove)
{
	string sourceString(TestBigString.data(), TestBigString.length());

	// Move c'tor
	string newString(std::move(sourceString));
	checkMovedString(sourceString);
	checkSameBigString(newString);

	// Reuse
	sourceString.assign(TestBigString.data(), TestBigString.length());
	checkSameBigString(sourceString);

	// Move assignment
	newString = std::move(sourceString);
	checkMovedString(sourceString);
	checkSameBigString(newString);
}

BOOST_AUTO_TEST_CASE(NewPoolMove)
{
	AutoMemoryPool pool(MemoryPool::createPool());
	string sourceString(*pool, TestBigString.data(), TestBigString.length());

	// Move c'tor
	string newString(*pool, std::move(sourceString));
	checkMovedString(sourceString);
	checkSameBigString(newString);

	// Reuse
	sourceString.assign(TestBigString.data(), TestBigString.length());
	checkSameBigString(sourceString);

	// Move assignment
	newString = std::move(sourceString);
	checkMovedString(sourceString);
	checkSameBigString(newString);
}

BOOST_AUTO_TEST_SUITE_END() // MoveString


BOOST_AUTO_TEST_SUITE(DoNotMoveString)

// Do not move
BOOST_AUTO_TEST_CASE(DifferentVsDefaultPoolMove)
{
	AutoMemoryPool pool(MemoryPool::createPool());
	string sourceString(*pool, TestBigString.data(), TestBigString.length());

	// Move c'tor
	string newString(std::move(sourceString));
	checkSameBigString(sourceString);
	checkSameBigString(newString);

	// Reuse
	sourceString.assign(TestBigString.data(), TestBigString.length());
	checkSameBigString(sourceString);

	// Move assignment
	newString = std::move(sourceString);
	checkSameBigString(sourceString);
	checkSameBigString(newString);
}


// Do not move
BOOST_AUTO_TEST_CASE(DifferentPoolsMove)
{
	AutoMemoryPool pool1(MemoryPool::createPool());
	AutoMemoryPool pool2(MemoryPool::createPool());
	string sourceString(*pool1, TestBigString.data(), TestBigString.length());

	// Move c'tor
	string newString(*pool2, std::move(sourceString));
	checkSameBigString(sourceString);
	checkSameBigString(newString);

	// Reuse
	sourceString.assign(TestBigString.data(), TestBigString.length());
	checkSameBigString(sourceString);

	// Move assignment
	newString = std::move(sourceString);
	checkSameBigString(sourceString);
	checkSameBigString(newString);
}


// Do not move
BOOST_AUTO_TEST_CASE(SmallStringMove)
{
	string sourceString(TestString.data(), TestString.length());

	// Move c'tor
	string newString(std::move(sourceString));
	checkSameString(sourceString);
	checkSameString(newString);

	// Reuse
	sourceString.assign(TestString.data(), TestString.length());
	BOOST_CHECK_EQUAL(std::string_view(sourceString.data(), sourceString.length()), TestString);

	// Move assign
	newString = std::move(sourceString);
	checkSameString(sourceString);
	checkSameString(newString);
}

BOOST_AUTO_TEST_SUITE_END() // DoNotMoveString

BOOST_AUTO_TEST_SUITE_END() // StringMoveSemantics
BOOST_AUTO_TEST_SUITE_END()	// MoveSuite
