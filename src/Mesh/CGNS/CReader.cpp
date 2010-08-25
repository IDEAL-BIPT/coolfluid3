#include <boost/algorithm/string/replace.hpp>
#include <boost/foreach.hpp>
#include <boost/progress.hpp>
#include <boost/filesystem/convenience.hpp>

#include "Common/ObjectProvider.hpp"
#include "Common/OptionT.hpp"
#include "Common/ComponentPredicates.hpp"
#include "Common/StringOps.hpp"
#include "Common/BasicExceptions.hpp"

#include "Mesh/CMesh.hpp"
#include "Mesh/CRegion.hpp"
#include "Mesh/CGNS/CReader.hpp"

//////////////////////////////////////////////////////////////////////////////

namespace CF {
namespace Mesh {
namespace CGNS {
  
using namespace Common;
  
////////////////////////////////////////////////////////////////////////////////

ObjectProvider < CReader,
                 CMeshReader,
                 CGNSLib,
                 1 >
aCGNSReader_Provider ( "CGNS" );

//////////////////////////////////////////////////////////////////////////////

CReader::CReader(const CName& name)
: CMeshReader(name), Shared()
{
  BUILD_COMPONENT;
}

//////////////////////////////////////////////////////////////////////////////

std::vector<std::string> CReader::get_extensions()
{
  std::vector<std::string> extensions;
  extensions.push_back(".cgns");
  return extensions;
}

//////////////////////////////////////////////////////////////////////////////

void CReader::defineConfigOptions ( OptionList& options )
{
  options.add< OptionT<bool> >
      ( "SectionsAreBCs",
        ("Treat Sections of lower dimensionality as BC. "
        "This means no BCs from cgns will be read"),
        false );
  options.add< OptionT<bool> >
  ( "SharedCoordinates",
   ("Store all the coordinates in 1 table. "
    "This means that there will be no coordinates per region"),
   true );
}

//////////////////////////////////////////////////////////////////////////////

void CReader::read_from_to(boost::filesystem::path& fp, const CMesh::Ptr& mesh)
{
  // Set the internal mesh pointer
  m_mesh = mesh;

  // Create basic region structure
  CRegion& regions = m_mesh->create_region(boost::filesystem::basename(fp));

  // open file in read mode
  CALL_CGNS(cg_open(fp.string().c_str(),CG_MODE_READ,&m_file.idx));

  // check how many bases we have
  CALL_CGNS(cg_nbases(m_file.idx,&m_file.nbBases));

  // Store if there is only 1 base
  m_base.unique = m_file.nbBases==1 ? true : false;
  
  // Read every base (usually there is only 1)
  for (m_base.idx = 1; m_base.idx<=m_file.nbBases; ++m_base.idx)
    read_base(regions);

  // close the CGNS file
  CALL_CGNS(cg_close(m_file.idx));

}

//////////////////////////////////////////////////////////////////////////////

void CReader::read_base(CRegion& parent_region)
{

  // get the name, dimension and physical dimension from the base
  char base_name_char[CGNS_CHAR_MAX];
  CALL_CGNS(cg_base_read(m_file.idx,m_base.idx,base_name_char,&m_base.cell_dim,&m_base.phys_dim));
  m_base.name=base_name_char;
  boost::algorithm::replace_all(m_base.name," ","_");
  boost::algorithm::replace_all(m_base.name,".","_");


  // create region for the base in mesh
  CRegion& base_region = m_base.unique ? parent_region : parent_region.create_region(m_base.name);

  
  // check how many zones we have
  CALL_CGNS(cg_nzones(m_file.idx,m_base.idx,&m_base.nbZones));
  m_zone.unique = m_base.nbZones == 1 ? true : false;
  // Read every zone in this base
  for (m_zone.idx = 1; m_zone.idx<=m_base.nbZones; ++m_zone.idx)
    read_zone(base_region);

}

//////////////////////////////////////////////////////////////////////////////

void CReader::read_zone(CRegion& parent_region)
{
  // get zone type (Structured or Unstructured)
  CALL_CGNS(cg_zone_type(m_file.idx,m_base.idx,m_zone.idx,&m_zone.type));
  
  // For now only Unstructured and Structured zone types are supported
  if (m_zone.type != Structured && m_zone.type != Unstructured) 
    throw NotImplemented (FromHere(),"Only Unstructured and Structured zone types are supported");

  // Read zone size and name
  if (m_zone.type == Unstructured)
  {
    int size[3][1];
    char zone_name_char[CGNS_CHAR_MAX];
    CALL_CGNS(cg_zone_read(m_file.idx,m_base.idx,m_zone.idx,zone_name_char,size[0]));
    m_zone.name = zone_name_char;
    boost::algorithm::replace_all(m_zone.name," ","_");
    boost::algorithm::replace_all(m_zone.name,".","_");

    m_zone.total_nbVertices = size[CGNS_VERT_IDX][0];
    m_zone.nbElements       = size[CGNS_CELL_IDX][0];
    m_zone.nbBdryVertices   = size[CGNS_BVRT_IDX][0];
    
    
    
    // get the number of grids
    CALL_CGNS(cg_ngrids(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbGrids));
    // nb coord dims
    CALL_CGNS(cg_ncoords(m_file.idx,m_base.idx,m_zone.idx, &m_zone.coord_dim));
    // find out number of solutions
    CALL_CGNS(cg_nsols(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbSols));
    // find out how many sections
    CALL_CGNS(cg_nsections(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbSections));
    m_section.unique = m_zone.nbSections == 1 ? true : false;
    // find out number of BCs that exist under this zone
    CALL_CGNS(cg_nbocos(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbBocos));
    m_boco.unique = m_zone.nbBocos == 1 ? true : false;
    // Add up all the nb elements from all sections
    m_zone.total_nbElements = get_total_nbElements();
    
    
    // Create a region for this zone if there is more than one
    CRegion& this_region = m_zone.unique? parent_region : parent_region.create_region(m_zone.name);
    
    // read coordinates in this zone
    for (int i=1; i<=m_zone.nbGrids; ++i)
      read_coordinates_unstructured(this_region);
    
    // read sections (or subregions) in this zone
    m_global_to_region.reserve(m_zone.total_nbElements);
    for (m_section.idx=1; m_section.idx<=m_zone.nbSections; ++m_section.idx)
      read_section(this_region);
    
//    // Only read boco's if sections are not defined as BC's
//    if (!option("SectionsAreBCs")->value<bool>())
//    {
      // read boundaryconditions (or subregions) in this zone
      for (m_boco.idx=1; m_boco.idx<=m_zone.nbBocos; ++m_boco.idx)
        read_boco_unstructured(this_region);
//      
//      // Remove regions flagged as bc
//      BOOST_FOREACH(CRegion& region, recursive_filtered_range_typed<CRegion>(this_region,IsComponentTag("remove_this_tmp_component")))
//      {
//        region.get_parent()->remove_component(region.name());
//      }
//    }
    
    // Cleanup:
    
    // truely deallocate the global_to_region vector
    m_global_to_region.resize(0);
    std::vector<Region_TableIndex_pair>().swap (m_global_to_region);
    
    
    
    
  }
  else if(m_zone.type == Structured)
  {
    int isize[3][3];
    char zone_name_char[CGNS_CHAR_MAX];
    CALL_CGNS(cg_zone_read(m_file.idx,m_base.idx,m_zone.idx,zone_name_char,isize[0]));
    m_zone.name = zone_name_char;
    boost::algorithm::replace_all(m_zone.name," ","_");
    boost::algorithm::replace_all(m_zone.name,".","_");

    m_zone.nbVertices[XX] = isize[CGNS_VERT_IDX][XX];
    m_zone.nbVertices[YY] = isize[CGNS_VERT_IDX][YY];
    m_zone.nbVertices[ZZ] = isize[CGNS_VERT_IDX][ZZ];
    
    // m_zone.nbElements     = size[CGNS_CELL_IDX];
    // m_zone.nbBdryVertices = size[CGNS_BVRT_IDX];

    // get the number of grids
    CALL_CGNS(cg_ngrids(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbGrids));
    // nb coord dims
    CALL_CGNS(cg_ncoords(m_file.idx,m_base.idx,m_zone.idx, &m_zone.coord_dim));
    m_zone.total_nbVertices = 1;
    for (int d=0; d<m_zone.coord_dim; ++d)
      m_zone.total_nbVertices *= m_zone.nbVertices[d];

    // find out number of solutions
    CALL_CGNS(cg_nsols(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbSols));
    // find out how many sections
    CALL_CGNS(cg_nsections(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbSections));
    m_section.unique = m_zone.nbSections == 1 ? true : false;
    // find out number of BCs that exist under this zone
    CALL_CGNS(cg_nbocos(m_file.idx,m_base.idx,m_zone.idx,&m_zone.nbBocos));
    m_boco.unique = m_zone.nbBocos == 1 ? true : false;
    // Add up all the nb elements from all sections
    m_zone.total_nbElements = get_total_nbElements();
        
    // Create a region for this zone if there is more than one
    CRegion& this_region = m_zone.unique? parent_region : parent_region.create_region(m_zone.name);
    
    // read coordinates in this zone
    for (int i=1; i<=m_zone.nbGrids; ++i)
      read_coordinates_structured(this_region);
    
    create_structured_elements(this_region);
    
    // read boundaryconditions (or subregions) in this zone
    for (m_boco.idx=1; m_boco.idx<=m_zone.nbBocos; ++m_boco.idx)
      read_boco_structured(this_region);
    
  }





}

//////////////////////////////////////////////////////////////////////////////

void CReader::read_coordinates_unstructured(CRegion& parent_region)
{

  CFinfo << "creating coordinates in " << parent_region.full_path().string() << CFendl;
  CArray& coordinates = parent_region.create_coordinates(m_zone.coord_dim);

  // read coordinates
  int one = 1;
  Real *xCoord;
  Real *yCoord;
  Real *zCoord;
  switch (m_zone.coord_dim)
  {
    case 3:
      zCoord = new Real[m_zone.total_nbVertices];
      CALL_CGNS(cg_coord_read(m_file.idx,m_base.idx,m_zone.idx, "CoordinateZ", RealDouble, &one, &m_zone.total_nbVertices, zCoord));
    case 2:
      yCoord = new Real[m_zone.total_nbVertices];
      CALL_CGNS(cg_coord_read(m_file.idx,m_base.idx,m_zone.idx, "CoordinateY", RealDouble, &one, &m_zone.total_nbVertices, yCoord));
    case 1:
      xCoord = new Real[m_zone.total_nbVertices];
      CALL_CGNS(cg_coord_read(m_file.idx,m_base.idx,m_zone.idx, "CoordinateX", RealDouble, &one, &m_zone.total_nbVertices, xCoord));
  }

  CArray::Buffer buffer = coordinates.create_buffer();
  buffer.increase_array_size(m_zone.total_nbVertices);
  std::vector<Real> row(m_zone.coord_dim);
  for (int i=0; i<m_zone.total_nbVertices; ++i)
  {
    switch (m_zone.coord_dim)
    {
      case 3:
        row[2] = zCoord[i];
       case 2:
        row[1] = yCoord[i];
       case 1:
        row[0] = xCoord[i];
     }
    buffer.add_row_directly(row);
  }

  switch (m_zone.coord_dim)
  {
    case 3:
      delete_ptr(zCoord);
    case 2:
      delete_ptr(yCoord);
    case 1:
      delete_ptr(xCoord);
  }
}

//////////////////////////////////////////////////////////////////////////////

void CReader::read_coordinates_structured(CRegion& parent_region)
{
  CArray& coordinates = parent_region.create_coordinates(m_zone.coord_dim);
  
  int one[3];
  one[0]= 1;
  one[1]= 1;
  one[2]= 1;

  // read coordinates
  Real *xCoord;
  Real *yCoord;
  Real *zCoord;
  switch (m_zone.coord_dim)
  {
    case 3:
      zCoord = new Real[m_zone.total_nbVertices];
      CALL_CGNS(cg_coord_read(m_file.idx,m_base.idx,m_zone.idx, "CoordinateZ", RealDouble, one, m_zone.nbVertices, zCoord));
    case 2:
      yCoord = new Real[m_zone.total_nbVertices];
      CALL_CGNS(cg_coord_read(m_file.idx,m_base.idx,m_zone.idx, "CoordinateY", RealDouble, one, m_zone.nbVertices, yCoord));
    case 1:
      xCoord = new Real[m_zone.total_nbVertices];
      CALL_CGNS(cg_coord_read(m_file.idx,m_base.idx,m_zone.idx, "CoordinateX", RealDouble, one, m_zone.nbVertices, xCoord));
  }
  
  CArray::Buffer buffer = coordinates.create_buffer();
  buffer.increase_array_size(m_zone.total_nbVertices);
  std::vector<Real> row(m_zone.coord_dim);

  switch (m_zone.coord_dim)
  {
    case 3:
      for (int k=0; k<m_zone.nbVertices[ZZ]; ++k)
        for (int j=0; j<m_zone.nbVertices[YY]; ++j)
          for (int i=0; i<m_zone.nbVertices[XX]; ++i)
          {
            row[0] = xCoord[structured_node_idx(i,j,k)];
            row[1] = yCoord[structured_node_idx(i,j,k)];
            row[2] = zCoord[structured_node_idx(i,j,k)];
            buffer.add_row_directly(row);
          }
      break;
    case 2:
      for (int j=0; j<m_zone.nbVertices[YY]; ++j)
        for (int i=0; i<m_zone.nbVertices[XX]; ++i)
        {
          row[0] = xCoord[structured_node_idx(i,j,0)];
          row[1] = yCoord[structured_node_idx(i,j,0)];
          buffer.add_row_directly(row);
        }
      break;
      
    case 1:
      for (int i=0; i<m_zone.nbVertices[XX]; ++i)
      {
        row[0] = xCoord[i];
        buffer.add_row_directly(row);
      }
      break;
  }

  delete_ptr(xCoord);
  delete_ptr(yCoord);
  delete_ptr(zCoord);
}
  
//////////////////////////////////////////////////////////////////////////////

void CReader::read_section(CRegion& parent_region)
{
  char section_name_char[CGNS_CHAR_MAX];
  
  // read section information
  cg_section_read(m_file.idx, m_base.idx, m_zone.idx, m_section.idx, section_name_char, &m_section.type,
                          &m_section.eBegin, &m_section.eEnd, &m_section.nbBdry, &m_section.parentFlag);
  m_section.name=section_name_char;
  
  // replace whitespace by underscore
  boost::algorithm::replace_all(m_section.name," ","_");
  boost::algorithm::replace_all(m_section.name,".","_");


  // Create a new region for this section
  CRegion& this_region = parent_region.create_region(m_section.name);
  CArray& all_coordinates = *parent_region.get_child_type<CArray>("coordinates");

  if (m_section.type == MIXED)
  {
    // Create CElements component for each element type.
    BufferMap buffer = create_element_regions_with_buffermap(this_region,
                                                             all_coordinates,
                                                             get_supported_element_types());
    
    // Handle each element of this section separately to see in which CElements component it will be written
    for (int elem=m_section.eBegin;elem<=m_section.eEnd;++elem)
    {
      // Read the amount of nodes this 1 element contains
      CALL_CGNS(cg_ElementPartialSize(m_file.idx,m_base.idx,m_zone.idx,m_section.idx,elem,elem,&m_section.elemNodeCount));
      m_section.elemNodeCount--; // subtract 1 as there is one index too many storing the element type

      // Storage for element type (index 0) and element nodes (index 1->elemNodeCount)
      int elemNodes[1][1+m_section.elemNodeCount];
      
      // Read nodes of 1 element
      CALL_CGNS(cg_elements_partial_read(m_file.idx,m_base.idx,m_zone.idx,m_section.idx,elem,elem,*elemNodes,&m_section.parentData));
      
      // Store the cgns element type
      ElementType_t etype_cgns = static_cast<ElementType_t>(elemNodes[0][0]);

      // Put the element nodes in a vector
      std::vector<Uint> row(m_section.elemNodeCount);
      for (int n=1;n<=m_section.elemNodeCount;++n)  // n=0 is the cell type
        row[n-1]=(elemNodes[0][n]-1); // -1 because cgns has index-base 1 instead of 0
      
      // Convert the cgns element type to the CF element type
      const std::string& etype_CF = m_elemtype_CGNS_to_CF[etype_cgns]+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";

      // Add the nodes to the correct CElements component using its buffer
      Uint table_idx = buffer[etype_CF]->add_row(row);
      
      // Store the global element number to a pair of (region , local element number)
      m_global_to_region.push_back(Region_TableIndex_pair(get_named_component_typed_ptr<CElements>(this_region, etype_CF),table_idx));
    } // for elem
  } // if mixed
  else // Single element type in this section
  {

    // Read the number of nodes in this section
    CALL_CGNS(cg_npe(m_section.type,&m_section.elemNodeCount));

    // Read the size of all elements
    CALL_CGNS(cg_ElementDataSize(m_file.idx,m_base.idx,m_zone.idx,m_section.idx,&m_section.elemDataSize	));

    // Calculate the number of elements
    int nbElems = m_section.elemDataSize/m_section.elemNodeCount;

    // Convert the CGNS element type to the CF element type
    const std::string& etype_CF = m_elemtype_CGNS_to_CF[m_section.type]+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";

    
    // Create element component in this region for this CF element type, automatically creates connectivity_table
    if (option("SharedCoordinates")->value<bool>())
      this_region.create_elements(etype_CF,all_coordinates);
    else
    {
      // Create coordinates component in this region for this CF element type
      CArray& section_coordinates = this_region.create_coordinates(m_zone.coord_dim);
      this_region.create_elements(etype_CF,section_coordinates); // no second argument defaults to the coordinates in this_region
    }

    CElements& element_region= *this_region.get_child_type<CElements>("elements_"+etype_CF);

    // Create a buffer for this element component, to start filling in the elements we will read.
    CTable::Buffer element_buffer = element_region.connectivity_table().create_buffer();
    CArray::Buffer coord_buffer   = element_region.coordinates().create_buffer();

    // Create storage for element nodes
    int* elemNodes = new int [m_section.elemDataSize];
    
    // Read in the element nodes
    cg_elements_read	(m_file.idx,m_base.idx,m_zone.idx,m_section.idx, elemNodes,&m_section.parentData);
    
    // --------------------------------------------- Fill connectivity table
    std::vector<Uint> coords_added;
    std::vector<Uint> row(m_section.elemNodeCount);
    element_buffer.increase_array_size(nbElems);  // we can use increase_array_size + add_row_directly because we know apriori the change
    Uint global_coord_idx;
    Uint local_coord_idx;
    for (int elem=0; elem<nbElems; ++elem) //, ++progress)
    {
      for (int node=0;node<m_section.elemNodeCount;++node) 
      {
        global_coord_idx = elemNodes[node+elem*m_section.elemNodeCount]-1;  // -1 because cgns has index-base 1 instead of 0
        if (option("SharedCoordinates")->value<bool>())
        {
          row[node] = global_coord_idx;
        }
        else
        {
          // check if this node was already added
          local_coord_idx = std::find(coords_added.begin(),coords_added.end(),global_coord_idx)-coords_added.begin();
          // if not found in coords_added, it has to be added to the buffer
          if (local_coord_idx >= coords_added.size())
          {
            local_coord_idx = coord_buffer.add_row(all_coordinates[global_coord_idx]);
            coords_added.push_back(global_coord_idx);
          }
          row[node] = local_coord_idx;
        }
      }
      element_buffer.add_row_directly(row);
      
      // Store the global element number to a pair of (region , local element number)
      m_global_to_region.push_back(Region_TableIndex_pair(boost::dynamic_pointer_cast<CElements>(element_region.shared_from_this()),elem));
    } // for elem
    
    // Delete storage for element nodes
    delete_ptr(elemNodes);
  } // else not mixed

  remove_empty_element_regions(this_region);

//  // Mark BC regions as temporary if option SectionsAreBCs is false
//  if (!option("SectionsAreBCs")->value<bool>() && option("SharedCoordinates")->value<bool>())
//  {
//    bool is_bc_region = false;
//    BOOST_FOREACH(const CElements& element_region, range_typed<CElements>(this_region))
//    {
//      if (element_region.element_type().dimensionality() < static_cast<Uint>(m_base.cell_dim))
//        is_bc_region = is_bc_region || true;
//    }
//    if (is_bc_region)
//      this_region.add_tag("remove_this_tmp_component");
//  }
}

//////////////////////////////////////////////////////////////////////////////

void CReader::create_structured_elements(CRegion& parent_region)
{
    
  CArray& coordinates = *parent_region.get_child_type<CArray>("coordinates");
      
  std::string etype_CF;
  switch (m_base.cell_dim)
  {
    case 3: // Hexahedrons
      etype_CF = "Hexa"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";      
      break;
    case 2: // Quadrilaterals
      etype_CF = "Quad"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
      break;
    case 1: // Segments
      etype_CF = "Line"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
    default:
      break;
  }
  
  CRegion& this_region = parent_region.create_region("Inner");
  CElements& element_region = this_region.create_elements(etype_CF,coordinates);

  // Create a buffer for this element component, to start filling in the elements we will read.
  CTable::Buffer element_buffer = element_region.connectivity_table().create_buffer();
  
  // --------------------------------------------- Fill connectivity table
  //std::vector<Uint> row(m_section.elemNodeCount);
  element_buffer.increase_array_size(m_zone.total_nbElements);  // we can use increase_array_size + add_row_directly because we know apriori the change

  switch (m_base.cell_dim)
  {
    case 3: // Hexahedrons
    {
      std::vector<Uint> row(8);
      for (int k=0; k<m_zone.nbVertices[ZZ]-1; ++k)
        for (int j=0; j<m_zone.nbVertices[YY]-1; ++j)
          for (int i=0; i<m_zone.nbVertices[XX]-1; ++i)
          {
            row[0] = structured_node_idx(i  ,j  ,k  );
            row[1] = structured_node_idx(i+1,j  ,k  );
            row[2] = structured_node_idx(i+1,j+1,k  );
            row[3] = structured_node_idx(i  ,j+1,k  );
            row[4] = structured_node_idx(i  ,j  ,k+1);
            row[5] = structured_node_idx(i+1,j  ,k+1);
            row[6] = structured_node_idx(i+1,j+1,k+1);
            row[7] = structured_node_idx(i  ,j+1,k+1);
            element_buffer.add_row_directly(row);

          }
      break;
    }
    case 2: // Quadrilaterals
    {
      std::vector<Uint> row(4);
      int k=0;
      for (int j=0; j<m_zone.nbVertices[YY]-1; ++j)
        for (int i=0; i<m_zone.nbVertices[XX]-1; ++i)
          {
            row[0] = structured_node_idx(i  ,j  ,k  );
            row[1] = structured_node_idx(i+1,j  ,k  );
            row[2] = structured_node_idx(i+1,j+1,k  );
            row[3] = structured_node_idx(i  ,j+1,k  );
            element_buffer.add_row_directly(row);
          }
      break;
    }
    case 1: // Segments
    {
      std::vector<Uint> row(2);
      int j=0, k=0;
      for (int i=0; i<m_zone.nbVertices[XX]-1; ++i)
      {
        row[0] = structured_node_idx(i  ,j  ,k  );
        row[1] = structured_node_idx(i+1,j  ,k  );
        element_buffer.add_row_directly(row);
      }
      break;
    }
    default:
      break;
  }
  
  //remove_empty_element_regions(this_region);
}
  
//////////////////////////////////////////////////////////////////////////////

void CReader::read_boco_unstructured(CRegion& parent_region)
{

  // Read the info for this boundary condition.
  char boco_name_char[CGNS_CHAR_MAX];
  cg_boco_info(m_file.idx, m_base.idx, m_zone.idx, m_boco.idx, boco_name_char, &m_boco.boco_type, &m_boco.ptset_type,
               &m_boco.nBC_elem, &m_boco.normalIndex, &m_boco.normalListFlag, &m_boco.normalDataType, &m_boco.nDataSet);
  m_boco.name = boco_name_char;
  
  // replace whitespace by underscore
  boost::algorithm::replace_all(m_boco.name," ","_");
  boost::algorithm::replace_all(m_boco.name,".","_");
  
  // Read the element ID's
  int* boco_elems = new int [m_boco.nBC_elem];
  void* NormalList(NULL);
  CALL_CGNS(cg_boco_read(m_file.idx, m_base.idx, m_zone.idx, m_boco.idx, boco_elems, NormalList));

  switch (m_boco.ptset_type)
  {
    case ElementRange : // all bc elements are within a range given by 2 global element numbers
    {
      if (m_zone.type != Unstructured)
        throw NotSupported(FromHere(),"CGNS: Boundary with pointset_type \"ElementRange\" is only supported for Unstructured grids");      

      // First do some simple checks to see if an entire region can be taken as a BC.
      CElements::Ptr first_elements = m_global_to_region[boco_elems[0]-1].first;
      CElements::Ptr last_elements = m_global_to_region[boco_elems[1]-1].first;
      if (first_elements->get_parent() == last_elements->get_parent())
      {
        CRegion::Ptr group_region = first_elements->get_parent()->get_type<CRegion>();
        if (group_region->recursive_elements_count() == Uint(boco_elems[1]-boco_elems[0]+1))
        {
          group_region->rename(m_boco.name);
          break;
        }
      }
      
      
      // Create a region inside mesh/regions/bc-regions with the name of the cgns boco.
      CRegion& this_region = parent_region.create_region(m_boco.name);
      CArray& coordinates = *parent_region.get_child_type<CArray>("coordinates");
      
      // Create CElements components for every possible element type supported.
      BufferMap buffer = create_element_regions_with_buffermap(this_region,coordinates,get_supported_element_types());
      
      for (int global_element=boco_elems[0]-1;global_element<boco_elems[1];++global_element)
      {
        // Check which region this global_element belongs to
        CElements::Ptr element_region = m_global_to_region[global_element].first;
        
        // Check the local element number in this region
        Uint local_element = m_global_to_region[global_element].second;
        
        // Add the local element to the correct CElements component through its buffer
        buffer[element_region->element_type().getElementTypeName()]->add_row(element_region->connectivity_table()[local_element]);
      }
      
      // Flush all buffers and remove empty element regions
      for (BufferMap::iterator it=buffer.begin(); it!=buffer.end(); ++it)
        it->second->flush();
      buffer.clear();
      
      remove_empty_element_regions(this_region);
      break;
    }
    case ElementList : // all bc elements are listed as global element numbers
    {
      if (m_zone.type != Unstructured)
        throw NotSupported(FromHere(),"CGNS: Boundary with pointset_type \"ElementList\" is only supported for Unstructured grids");
      
      // First do some simple checks to see if an entire region can be taken as a BC.
      CElements::Ptr first_elements = m_global_to_region[boco_elems[0]-1].first;
      CElements::Ptr last_elements = m_global_to_region[boco_elems[m_boco.nBC_elem-1]-1].first;
      if (first_elements->get_parent() == last_elements->get_parent())
      {
        CRegion::Ptr group_region = first_elements->get_parent()->get_type<CRegion>();
        if (group_region->recursive_elements_count() == Uint(boco_elems[m_boco.nBC_elem-1]-boco_elems[0]+1))
        {
          group_region->rename(m_boco.name);
          break;
        }
      }
      
      // Create a region inside mesh/regions/bc-regions with the name of the cgns boco.
      CRegion& this_region = parent_region.create_region(m_boco.name);
      CArray& coordinates = *parent_region.get_child_type<CArray>("coordinates");
      
      // Create CElements components for every possible element type supported.
      BufferMap buffer = create_element_regions_with_buffermap(this_region,coordinates,get_supported_element_types());
      
      for (int i=0; i<m_boco.nBC_elem; ++i)
      {
        Uint global_element = boco_elems[i]-1;
        
        // Check which region this global_element belongs to
        CElements::Ptr element_region = m_global_to_region[global_element].first;
        
        // Check the local element number in this region
        Uint local_element = m_global_to_region[global_element].second;
        
        // Add the local element to the correct CElements component through its buffer
        buffer[element_region->element_type().getElementTypeName()]->add_row(element_region->connectivity_table()[local_element]);
      }
      
      // Flush all buffers and remove empty element regions
      for (BufferMap::iterator it=buffer.begin(); it!=buffer.end(); ++it)
        it->second->flush();
      buffer.clear();
      
      remove_empty_element_regions(this_region);
      
      break;
    }
    case PointRange : // bc elements are given by node index range
      throw NotSupported(FromHere(),"CGNS: Boundary with pointset_type \"PointRange\" is only supported for Structured grids");
    case PointList : // bc elements are given by node index list
      throw NotSupported(FromHere(),"CGNS: Boundary with pointset_type \"PointList\" is only supported for Structured grids");
    default :
      throw NotImplemented(FromHere(),"CGNS: no boundary with pointset_type " + StringOps::to_str<int>(m_boco.ptset_type) + " supported in CF yet"); 
  }

  delete_ptr(boco_elems);
}

//////////////////////////////////////////////////////////////////////////////

void CReader::read_boco_structured(CRegion& parent_region)
{
  
  // Read the info for this boundary condition.
  char boco_name_char[CGNS_CHAR_MAX];
  cg_boco_info(m_file.idx, m_base.idx, m_zone.idx, m_boco.idx, boco_name_char, &m_boco.boco_type, &m_boco.ptset_type,
               &m_boco.nBC_elem, &m_boco.normalIndex, &m_boco.normalListFlag, &m_boco.normalDataType, &m_boco.nDataSet);
  m_boco.name = boco_name_char;
  
  // replace whitespace by underscore
  boost::algorithm::replace_all(m_boco.name," ","_");
  boost::algorithm::replace_all(m_boco.name,".","_");
  
  // Create a region inside mesh/regions/bc-regions with the name of the cgns boco.
  CRegion& this_region = parent_region.create_region(m_boco.name);
  CArray& coordinates = *parent_region.get_child_type<CArray>("coordinates");
  
  // Which BC_element type will we need?
  std::string etype_CF;
  std::string etypeBC_CF;
  switch (m_base.cell_dim)
  {
    case 3: // Hexahedrons
      etype_CF = "Hexa"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
      etypeBC_CF = "Quad"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
      break;
    case 2: // Quadrilaterals
      etype_CF = "Quad"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
      etypeBC_CF = "Line"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
      break;
    case 1: // Segments
      etype_CF = "Line"+StringOps::to_str<int>(m_base.phys_dim)+"DLagrangeP1";
      etypeBC_CF = "Point"+StringOps::to_str<int>(m_base.phys_dim)+"D";
    default:
      break;
  }
  
  CElements& elements = this_region.create_elements(etypeBC_CF,coordinates);
  //CTable& source_elements = parent_region.get_child("Inner")->get_child_type<CElements>("elements_"+etype_CF)->connectivity_table();
  CTable::Buffer buffer = elements.connectivity_table().create_buffer();
  
  
  
  // Read the Node ID's
  int* boco_elems = new int [m_boco.nBC_elem*m_base.cell_dim];
  void* NormalList(NULL);
  CALL_CGNS(cg_boco_read(m_file.idx, m_base.idx, m_zone.idx, m_boco.idx, boco_elems, NormalList));
  
  
  switch (m_boco.ptset_type)
  {
    case ElementRange : // all bc elements are within a range given by 2 global element numbers
      throw NotSupported(FromHere(),"CGNS: Boundary with pointset_type \"ElementRange\" is only supported for Unstructured grids");
    case ElementList : // all bc elements are listed as global element numbers
      throw NotSupported(FromHere(),"CGNS: Boundary with pointset_type \"ElementList\" is only supported for Unstructured grids");
    case PointRange : // bc elements are given by node index range
    {
      int imin(0), jmin(0), kmin(0);
      int imax(0), jmax(0), kmax(0);
      
      imin=boco_elems[XX+0*m_base.cell_dim]-1; imax=boco_elems[XX+1*m_base.cell_dim]-1;
      jmin=boco_elems[YY+0*m_base.cell_dim]-1; jmax=boco_elems[YY+1*m_base.cell_dim]-1;
      kmin=boco_elems[ZZ+0*m_base.cell_dim]-1; kmax=boco_elems[ZZ+1*m_base.cell_dim]-1;
      
      std::vector<Uint> row(4);
      if (imin == imax) // i = constant plane
      {
        m_boco.nBC_elem = (jmax-jmin)*(kmax-kmin);
        buffer.increase_array_size(m_boco.nBC_elem);
        if (imin == 0)
          for (int k=kmin; k<kmax; ++k)
            for (int j=jmin; j<jmax; ++j)
            {
              row[0] = structured_node_idx(imin, j  , k  );
              row[1] = structured_node_idx(imin, j  , k+1);
              row[2] = structured_node_idx(imin, j+1, k+1);
              row[3] = structured_node_idx(imin, j+1, k  );
              buffer.add_row_directly(row);
            }
        else
          for (int k=kmin; k<kmax; ++k)
            for (int j=jmin; j<jmax; ++j)
            {
              row[0] = structured_node_idx(imax, j  ,k  );
              row[1] = structured_node_idx(imax ,j+1,k  );
              row[2] = structured_node_idx(imax, j+1,k+1);
              row[3] = structured_node_idx(imax, j  ,k+1);
              buffer.add_row_directly(row);
            }        
      }
      
      if (jmin == jmax) // j = constant plane
      {
        m_boco.nBC_elem = (imax-imin)*(kmax-kmin);
        buffer.increase_array_size(m_boco.nBC_elem);
        if (jmin == 0)
          for (int k=kmin; k<kmax; ++k)
            for (int i=imin; i<imax; ++i)
            {              
              row[0] = structured_node_idx(i  , jmin, k  );
              row[1] = structured_node_idx(i+1, jmin, k  );
              row[2] = structured_node_idx(i+1, jmin, k+1);
              row[3] = structured_node_idx(i  , jmin, k+1);
              buffer.add_row_directly(row);              
            }
        else
          for (int k=kmin; k<kmax; ++k)
            for (int i=imin; i<imax; ++i)
            {
              row[0] = structured_node_idx(i  ,jmax, k  );
              row[1] = structured_node_idx(i  ,jmax, k+1);
              row[2] = structured_node_idx(i+1,jmax, k+1);
              row[3] = structured_node_idx(i+1,jmax, k  );
              buffer.add_row_directly(row);              
            }
      }
      
      if (kmin == kmax) // k = constant plane
      {
        m_boco.nBC_elem = (imax-imin)*(jmax-jmin);
        buffer.increase_array_size(m_boco.nBC_elem);
        if (kmin == 0)
          for (int i=imin; i<imax; ++i)
            for (int j=jmin; j<jmax; ++j)
            {
              row[0] = structured_node_idx(i  ,j  , kmin  );
              row[1] = structured_node_idx(i  ,j+1, kmin  );
              row[2] = structured_node_idx(i+1,j+1, kmin  );
              row[3] = structured_node_idx(i+1,j  , kmin  );
              buffer.add_row_directly(row);            
            }
        else
          for (int i=imin; i<imax; ++i)
            for (int j=jmin; j<jmax; ++j)
            {
              row[0] = structured_node_idx(i  ,j  , kmax);
              row[1] = structured_node_idx(i+1,j  , kmax);
              row[2] = structured_node_idx(i+1,j+1, kmax);
              row[3] = structured_node_idx(i  ,j+1, kmax);
              buffer.add_row_directly(row);            
            }
      }
      buffer.flush();
      delete_ptr(boco_elems);
      
      break;
    }
    case PointList : // bc elements are given by node index list
    default :
      throw NotImplemented(FromHere(),"CGNS: no boundary with pointset_type " + StringOps::to_str<int>(m_boco.ptset_type) + " supported in CF yet"); 
  }
  
  
  remove_empty_element_regions(this_region);
  
}

//////////////////////////////////////////////////////////////////////////////
  
Uint CReader::get_total_nbElements()
{
  Uint nbElements;

  if (m_zone.type == Unstructured)
  {
    nbElements=0;
    // read sections (or subregions) in this zone
    for (m_section.idx=1; m_section.idx<=m_zone.nbSections; ++m_section.idx)
    {
      char section_name_char[CGNS_CHAR_MAX];
      CALL_CGNS(cg_section_read(m_file.idx, m_base.idx, m_zone.idx, m_section.idx, section_name_char, &m_section.type,
                                &m_section.eBegin, &m_section.eEnd, &m_section.nbBdry, &m_section.parentFlag));
      nbElements += m_section.eEnd - m_section.eBegin + 1;
    }
  }
  else if (m_zone.type == Structured)
  {
    nbElements=1;
    for (int d=0; d<m_zone.coord_dim; ++d)
      nbElements *= m_zone.nbVertices[d]-1;
  }
  
  return nbElements;
}


//////////////////////////////////////////////////////////////////////////////

} // CGNS
} // Mesh
} // CF
