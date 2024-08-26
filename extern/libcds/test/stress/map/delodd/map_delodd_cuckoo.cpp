// Copyright (c) 2006-2018 Maxim Khizhinsky
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "map_delodd.h"
#include "map_type_cuckoo.h"

namespace map {

    CDSSTRESS_CuckooMap( Map_DelOdd, run_test, key_thread, size_t )

} // namespace map
