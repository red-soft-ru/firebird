// Copyright (c) 2006-2018 Maxim Khizhinsky
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef CDSTEST_STAT_SPLITLIST_OUT_H
#define CDSTEST_STAT_SPLITLIST_OUT_H

#include <cds/intrusive/details/split_list_base.h>

namespace cds_test {

    static inline property_stream& operator <<( property_stream& o, cds::intrusive::split_list::empty_stat const& /*s*/ )
    {
        return o;
    }

    static inline property_stream& operator <<( property_stream& o, cds::intrusive::split_list::stat<> const& s )
    {
        return o
            << CDSSTRESS_STAT_OUT( s, m_nInsertSuccess )
            << CDSSTRESS_STAT_OUT( s, m_nInsertFailed )
            << CDSSTRESS_STAT_OUT( s, m_nUpdateExist )
            << CDSSTRESS_STAT_OUT( s, m_nUpdateNew )
            << CDSSTRESS_STAT_OUT( s, m_nExtractSuccess )
            << CDSSTRESS_STAT_OUT( s, m_nExtractFailed )
            << CDSSTRESS_STAT_OUT( s, m_nEraseSuccess )
            << CDSSTRESS_STAT_OUT( s, m_nEraseFailed )
            << CDSSTRESS_STAT_OUT( s, m_nFindSuccess )
            << CDSSTRESS_STAT_OUT( s, m_nFindFailed )
            << CDSSTRESS_STAT_OUT( s, m_nHeadNodeAllocated )
            << CDSSTRESS_STAT_OUT( s, m_nHeadNodeFreed )
            << CDSSTRESS_STAT_OUT( s, m_nBucketCount )
            << CDSSTRESS_STAT_OUT( s, m_nInitBucketRecursive )
            << CDSSTRESS_STAT_OUT( s, m_nInitBucketContention )
            << CDSSTRESS_STAT_OUT( s, m_nBucketsExhausted );
    }

} // namespace cds_test

#endif // #ifndef CDSTEST_STAT_SPLITLIST_OUT_H
