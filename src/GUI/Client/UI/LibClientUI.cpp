// Copyright (C) 2010 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "Common/RegistLibrary.hpp"

#include "GUI/Client/UI/LibClientUI.hpp"

namespace CF {
namespace GUI {
namespace ClientUI {

CF::Common::RegistLibrary<LibClientUI> libClientUI;

////////////////////////////////////////////////////////////////////////////////

void LibClientUI::initiate()
{
  cf_assert( !m_is_initiated );
  m_is_initiated = true;
}

void LibClientUI::terminate()
{
  m_is_initiated = false;
}

////////////////////////////////////////////////////////////////////////////////

} // Client
} // GUI
} // CF
