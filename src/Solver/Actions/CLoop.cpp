// Copyright (C) 2010 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "Common/URI.hpp"
#include "Common/CreateComponent.hpp"

#include "Common/OptionArray.hpp"

#include "Solver/Actions/CLoop.hpp"

#include "Mesh/CRegion.hpp"

/////////////////////////////////////////////////////////////////////////////////////

using namespace CF::Common;
using namespace CF::Mesh;

namespace CF {
namespace Solver {
namespace Actions {

/////////////////////////////////////////////////////////////////////////////////////

CLoop::CLoop ( const std::string& name ) :
  Common::CAction(name)
{
  mark_basic();
  std::vector< URI > dummy;
  m_properties.add_option< OptionArrayT < URI > > ("Regions", "Regions to loop over", dummy);

  m_properties["Regions"].as_option().attach_trigger ( boost::bind ( &CLoop::trigger_Regions,   this ) );
}

/////////////////////////////////////////////////////////////////////////////////////

CLoopOperation& CLoop::create_loop_operation(const std::string action_provider)
{
  // The execuation of operations must be in chronological order,
  // hence they get an alphabetical name
  std::string name = action_provider;
  CLoopOperation::Ptr sub_operation =
    (create_component_abstract_type<CLoopOperation>(action_provider,name));
  add_component(sub_operation);
  return *sub_operation;
}

/////////////////////////////////////////////////////////////////////////////////////

void CLoop::trigger_Regions()
{
  std::vector<URI> vec; property("Regions").put_value(vec);
  BOOST_FOREACH(const URI region_path, vec)
  {
    Component::Ptr comp = access_component_ptr(region_path)->as_ptr<CRegion>();
    if ( is_null(comp) )
    {
      throw ValueNotFound ( FromHere(), "Could not find region with path [" + region_path.path() +"]" );
    }
    m_loop_regions.push_back(access_component_ptr(region_path)->as_ptr<CRegion>());
  }
}

/////////////////////////////////////////////////////////////////////////////////////

const CLoopOperation& CLoop::action(const std::string& name) const
{
  return *get_child_ptr(name)->as_ptr<CLoopOperation>();
}

/////////////////////////////////////////////////////////////////////////////////////

CLoopOperation& CLoop::action(const std::string& name)
{
  return *get_child_ptr(name)->as_ptr<CLoopOperation>();
}

/////////////////////////////////////////////////////////////////////////////////////

} // Actions
} // Solver
} // CF

/////////////////////////////////////////////////////////////////////////////////////
