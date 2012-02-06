// Copyright (C) 2010-2011 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include <cstdio>
#include <cstdlib>

#include "common/PE/ListeningInfo.hpp"

////////////////////////////////////////////////////////////////////////////

namespace cf3 {
namespace common {
namespace PE {

////////////////////////////////////////////////////////////////////////////

ListeningInfo::ListeningInfo()
  : ready(true)
{
  data = (char *) std::malloc( buffer_size() );
}

////////////////////////////////////////////////////////////////////////////

ListeningInfo::~ListeningInfo()
{
  std::free(data);
}

////////////////////////////////////////////////////////////////////////////

} // PE
} // common
} // cf3

/////////////////////////////////////////////////////////////////////////////