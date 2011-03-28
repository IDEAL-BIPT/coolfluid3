// Copyright (C) 2010 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "Common/RegistLibrary.hpp"

#include "Tools/FieldGeneration/LibFieldGeneration.hpp"

namespace CF {
namespace Tools {
namespace FieldGeneration {

CF::Common::RegistLibrary<LibFieldGeneration> libFieldGeneration;

////////////////////////////////////////////////////////////////////////////////

void LibFieldGeneration::initiate()
{
  cf_assert( !m_is_initiated );
  m_is_initiated = true;
}

void LibFieldGeneration::terminate()
{
  m_is_initiated = false;
}

////////////////////////////////////////////////////////////////////////////////

} // LibFieldGeneration
} // Tools
} // CF
