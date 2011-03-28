// Copyright (C) 2010 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "Tools/GooglePerf/LibGooglePerfTools.hpp"

#include "Common/Core.hpp"
#include "Common/RegistLibrary.hpp"

namespace CF {
namespace Tools {
namespace GooglePerf {

CF::Common::RegistLibrary<LibGooglePerfTools> libGooglePerfTools;

void LibGooglePerfTools::initiate()
{
  cf_assert( !m_is_initiated );
  m_is_initiated = true;
}

void LibGooglePerfTools::terminate()
{
  m_is_initiated = false;
}

} // GooglePerf
} // Tools
} // CF
