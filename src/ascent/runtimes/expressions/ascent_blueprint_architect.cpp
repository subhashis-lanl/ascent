//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2015-2019, Lawrence Livermore National Security, LLC.
//
// Produced at the Lawrence Livermore National Laboratory
//
// LLNL-CODE-716457
//
// All rights reserved.
//
// This file is part of Ascent.
//
// For details, see: http://ascent.readthedocs.io/.
//
// Please also read ascent/LICENSE
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the disclaimer below.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the disclaimer (as noted below) in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of the LLNS/LLNL nor the names of its contributors may
//   be used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL LAWRENCE LIVERMORE NATIONAL SECURITY,
// LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//


//-----------------------------------------------------------------------------
///
/// file: ascent_blueprint_architect.cpp
///
//-----------------------------------------------------------------------------

#include "ascent_blueprint_architect.hpp"
#include "ascent_conduit_reductions.hpp"

#include <ascent_logging.hpp>
#include <ascent_mpi_utils.hpp>

#include <cstring>
#include <limits>
#include <cmath>

#include <flow_workspace.hpp>

#ifdef ASCENT_MPI_ENABLED
#include <mpi.h>
#include <conduit_relay_mpi.hpp>
#endif

//-----------------------------------------------------------------------------
// -- begin ascent:: --
//-----------------------------------------------------------------------------
namespace ascent
{

//-----------------------------------------------------------------------------
// -- begin ascent::runtime --
//-----------------------------------------------------------------------------
namespace runtime
{

//-----------------------------------------------------------------------------
// -- begin ascent::runtime::expressions--
//-----------------------------------------------------------------------------
namespace expressions
{

namespace detail
{

bool at_least_one(bool local)
{
  bool agreement = local;
#ifdef ASCENT_MPI_ENABLED
  int local_boolean = local ? 1 : 0;
  int global_count = 0;
  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Allreduce((void *)(&local_boolean),
                (void *)(&global_count),
                1,
                MPI_INT,
                MPI_SUM,
                mpi_comm);

  if(global_count > 0)
  {
    agreement = true;
  }
#endif
  return agreement;
}

struct UniformCoords
{
  conduit::float64 m_origin[3] = {0., 0., 0.};
  conduit::float64 m_spacing[3] = {1., 1., 1.};
  int m_dims[3] = {0,0,0};
  bool m_is_2d = true;

  UniformCoords(const conduit::Node &n_coords)
  {
    populate(n_coords);
  }

  void populate(const conduit::Node &n_coords)
  {

    const conduit::Node &n_dims = n_coords["dims"];

    m_dims[0] = n_dims["i"].to_int();
    m_dims[1] = n_dims["j"].to_int();
    m_dims[2] = 1;

    // check for 3d
    if(n_dims.has_path("k"))
    {
        m_dims[2] = n_dims["k"].to_int();
        m_is_2d = false;
    }


    const conduit::Node &n_origin = n_coords["origin"];

    m_origin[0] = n_origin["x"].to_float64();
    m_origin[1] = n_origin["y"].to_float64();

    if(n_origin.has_child("z"))
    {
      m_origin[2] = n_origin["z"].to_float64();
    }

    const conduit::Node &n_spacing = n_coords["spacing"];

    m_spacing[0] = n_spacing["dx"].to_float64();
    m_spacing[1] = n_spacing["dy"].to_float64();

    if(n_spacing.has_path("dz"))
    {
      m_spacing[2] = n_spacing["dz"].to_float64();
    }
  }
};

int
get_num_indices(const std::string shape_type)
{
  int num = 0;
  if(shape_type == "tri")
  {
      num = 3;
  }
  else if(shape_type == "quad")
  {
      num = 4;
  }
  else if(shape_type == "tet")
  {
      num = 4;
  }
  else if(shape_type == "hex")
  {
      num = 8;
  }
  else if(shape_type == "point")
  {
      num = 1;
  }
  else
  {
    ASCENT_ERROR("Unsupported element type "<<shape_type);
  }
  return num;
}

void logical_index_2d(int *idx, const int vert_index, const int *dims)
{
  idx[0] = vert_index % dims[0];
  idx[1] = vert_index / dims[0];
}

void logical_index_3d(int *idx, const int vert_index, const int *dims)
{
  idx[0] = vert_index % dims[0];
  idx[1] = (vert_index / dims[0]) % dims[1];
  idx[2] = vert_index / (dims[0] * dims[1]);
}

void get_element_indices(const conduit::Node &n_topo,
                         const int index,
                         std::vector<int> &indices)
{

  const std::string mesh_type = n_topo["type"].as_string();
  if(mesh_type == "unstructured")
  {
    // supports only single element type
    const conduit::Node &n_topo_eles = n_topo["elements"];

    // get the shape
    const std::string ele_shape = n_topo_eles["shape"].as_string();
    const int num_indices = get_num_indices(ele_shape);

    indices.resize(num_indices);
    // look up the connectivity
    const conduit::Node &n_topo_conn = n_topo_eles["connectivity"];
    const conduit::int32_array conn_a = n_topo_conn.value();
    const int offset = index * num_indices;
    for(int i = 0; i < num_indices; ++i)
    {
      indices.push_back(conn_a[offset + i]);
    }
  }
  else
  {
    bool is_2d = true;
    int vert_dims[3] = {0, 0, 0};
    vert_dims[0] = n_topo["elements/dims/i"].to_int32() + 1;
    vert_dims[1] = n_topo["elements/dims/j"].to_int32() + 1;

    if(n_topo.has_path("elements/dims/j"))
    {
      vert_dims[2] = n_topo["elements/dims/k"].to_int32() + 1;
      is_2d = false;
    }

    const int element_dims[3] = {vert_dims[0] - 1,
                              vert_dims[1] - 1,
                              vert_dims[2] - 1};

    int element_index[3] = {0, 0, 0};
    if(is_2d)
    {
      indices.resize(4);
      logical_index_2d(element_index, index, element_dims);

      indices[0] = element_index[1] * vert_dims[0] + element_index[0];
      indices[1] = indices[0] + 1;
      indices[2] = indices[1] + vert_dims[0];
      indices[3] = indices[2] - 1;
    }
    else
    {
      indices.resize(8);
      logical_index_3d(element_index, index, element_dims);

      indices[0] = (element_index[2] * vert_dims[1] + element_index[1]) * vert_dims[0] + element_index[0];
      indices[1] = indices[0] + 1;
      indices[2] = indices[1] + vert_dims[1];
      indices[3] = indices[2] - 1;
      indices[4] = indices[0] + vert_dims[0] * vert_dims[2];
      indices[5] = indices[4] + 1;
      indices[6] = indices[5] + vert_dims[1];
      indices[7] = indices[6] - 1;
    }


  }
}


conduit::Node
get_uniform_vert(const conduit::Node &n_coords, const int &index)
{

  UniformCoords coords(n_coords);

  int logical_index[3] = {0, 0, 0};

  if(coords.m_is_2d)
  {
    logical_index_2d(logical_index, index, coords.m_dims);
  }
  else
  {
    logical_index_3d(logical_index, index, coords.m_dims);
  }

  double vert[3];
  vert[0] = coords.m_origin[0] + logical_index[0] * coords.m_spacing[0];
  vert[1] = coords.m_origin[1] + logical_index[1] * coords.m_spacing[1];
  vert[2] = coords.m_origin[2] + logical_index[2] * coords.m_spacing[2];

  conduit::Node res;
  res.set(vert,3);
  return res;
}

conduit::Node
get_explicit_vert(const conduit::Node &n_coords, const int &index)
{
  bool is_float64 = true;
  if(n_coords["values/x"].dtype().is_float32())
  {
    is_float64 = false;
  }
  double vert[3] = {0., 0., 0.};
  if(is_float64)
  {
    conduit::float64_array x_a = n_coords["values/x"].value();
    conduit::float64_array y_a = n_coords["values/y"].value();
    conduit::float64_array z_a = n_coords["values/z"].value();
    vert[0] = x_a[index];
    vert[1] = y_a[index];
    vert[2] = z_a[index];
  }
  else
  {
    conduit::float32_array x_a = n_coords["values/x"].value();
    conduit::float32_array y_a = n_coords["values/y"].value();
    conduit::float32_array z_a = n_coords["values/z"].value();
    vert[0] = x_a[index];
    vert[1] = y_a[index];
    vert[2] = z_a[index];

  }

  conduit::Node res;
  res.set(vert,3);
  return res;
}

conduit::Node
get_rectilinear_vert(const conduit::Node &n_coords, const int &index)
{
  bool is_float64 = true;

  int dims[3] = {0,0,0};
  dims[0] = n_coords["values/x"].dtype().number_of_elements();
  dims[1] = n_coords["values/y"].dtype().number_of_elements();

  if(n_coords.has_path("values/z"))
  {
    dims[2] = n_coords["values/z"].dtype().number_of_elements();
  }

  if(n_coords["values/x"].dtype().is_float32())
  {
    is_float64 = false;
  }
  double vert[3] = {0., 0., 0.};


  int logical_index[3] = {0, 0, 0};

  if(dims[2] == 0)
  {
    logical_index_2d(logical_index, index, dims);
  }
  else
  {
    logical_index_3d(logical_index, index, dims);
  }

  if(is_float64)
  {
    conduit::float64_array x_a = n_coords["values/x"].value();
    conduit::float64_array y_a = n_coords["values/y"].value();
    vert[0] = x_a[logical_index[0]];
    vert[1] = y_a[logical_index[1]];
    if(dims[2] != 0)
    {
      conduit::float64_array z_a = n_coords["values/z"].value();
      vert[2] = z_a[logical_index[2]];
    }
  }
  else
  {
    conduit::float32_array x_a = n_coords["values/x"].value();
    conduit::float32_array y_a = n_coords["values/y"].value();
    vert[0] = x_a[logical_index[0]];
    vert[1] = y_a[logical_index[1]];
    if(dims[2] != 0)
    {
      conduit::float32_array z_a = n_coords["values/z"].value();
      vert[2] = z_a[logical_index[2]];
    }

  }

  conduit::Node res;
  res.set(vert,3);
  return res;
}
// ----------------------  element locations ---------------------------------
conduit::Node
get_uniform_element(const conduit::Node &n_coords, const int &index)
{

  UniformCoords coords(n_coords);

  int logical_index[3] = {0, 0, 0};
  const int element_dims[3] = {coords.m_dims[0] - 1,
                            coords.m_dims[1] - 1,
                            coords.m_dims[2] - 1};

  if(coords.m_is_2d)
  {
    logical_index_2d(logical_index, index, element_dims);
  }
  else
  {
    logical_index_3d(logical_index, index, element_dims);
  }

  // element logical index will be the lower left point index

  double vert[3] = {0., 0., 0.};
  vert[0] = coords.m_origin[0] + logical_index[0] * coords.m_spacing[0] + coords.m_spacing[0] * 0.5;
  vert[1] = coords.m_origin[1] + logical_index[1] * coords.m_spacing[1] + coords.m_spacing[1] * 0.5;
  vert[2] = coords.m_origin[2] + logical_index[2] * coords.m_spacing[2] + coords.m_spacing[2] * 0.5;

  conduit::Node res;
  res.set(vert,3);
  return res;
}

conduit::Node
get_rectilinear_element(const conduit::Node &n_coords, const int &index)
{
  bool is_float64 = true;

  int dims[3] = {0,0,0};
  dims[0] = n_coords["values/x"].dtype().number_of_elements();
  dims[1] = n_coords["values/y"].dtype().number_of_elements();


  if(n_coords.has_path("values/z"))
  {
    dims[2] = n_coords["values/z"].dtype().number_of_elements();
  }

  if(n_coords["values/x"].dtype().is_float32())
  {
    is_float64 = false;
  }
  const int element_dims[3] = {dims[0] - 1,
                            dims[1] - 1,
                            dims[2] - 1};

  double vert[3] = {0., 0., 0.};

  int logical_index[3] = {0, 0, 0};

  if(dims[2] == 0)
  {
    logical_index_2d(logical_index, index, element_dims);
  }
  else
  {
    logical_index_3d(logical_index, index, element_dims);
  }

  if(is_float64)
  {
    conduit::float64_array x_a = n_coords["values/x"].value();
    conduit::float64_array y_a = n_coords["values/y"].value();
    vert[0] = (x_a[logical_index[0]] + x_a[logical_index[0] + 1]) * 0.5;
    vert[1] = (y_a[logical_index[1]] + y_a[logical_index[1] + 1]) * 0.5;
    if(dims[2] != 0)
    {
      conduit::float64_array z_a = n_coords["values/z"].value();
      vert[2] = (z_a[logical_index[2]] + z_a[logical_index[2] + 1]) * 0.5;
    }
  }
  else
  {
    conduit::float32_array x_a = n_coords["values/x"].value();
    conduit::float32_array y_a = n_coords["values/y"].value();
    vert[0] = (x_a[logical_index[0]] + x_a[logical_index[0] + 1]) * 0.5;
    vert[1] = (y_a[logical_index[1]] + y_a[logical_index[1] + 1]) * 0.5;
    if(dims[2] != 0)
    {
      conduit::float32_array z_a = n_coords["values/z"].value();
      vert[2] = (z_a[logical_index[2]] + z_a[logical_index[2] + 1]) * 0.5;
    }

  }

  conduit::Node res;
  res.set(vert,3);
  return res;
}

conduit::Node
get_explicit_element(const conduit::Node &n_coords,
                  const conduit::Node &n_topo,
                  const int &index)
{
  std::vector<int> conn;
  get_element_indices(n_topo, index, conn);
  const int num_indices = conn.size();
  double vert[3] = {0., 0., 0.};
  for(int i = 0; i < num_indices; ++i)
  {
    int vert_index = conn[i];
    conduit::Node n_vert = get_explicit_vert(n_coords, vert_index);
    double * ptr = n_vert.value();
    vert[0] += ptr[0];
    vert[1] += ptr[1];
    vert[2] += ptr[2];
  }

  vert[0] /= double(num_indices);
  vert[1] /= double(num_indices);
  vert[2] /= double(num_indices);

  conduit::Node res;
  res.set(vert,3);
  return res;
}
//-----------------------------------------------------------------------------
};
//-----------------------------------------------------------------------------
// -- end ascent::runtime::expressions::detail--
//-----------------------------------------------------------------------------

conduit::Node
vert_location(const conduit::Node &domain,
              const int &index,
              const std::string topo_name)
{
  std::string topo = topo_name;
  // if we don't specify a topology, find the first topology ...
  if(topo_name == "")
  {
      conduit::NodeConstIterator itr = domain["topologies"].children();
      itr.next();
      topo = itr.name();
  }

  const conduit::Node &n_topo   = domain["topologies"][topo];
  const std::string mesh_type   = n_topo["type"].as_string();
  const std::string coords_name = n_topo["coordset"].as_string();

  const conduit::Node &n_coords = domain["coordsets"][coords_name];

  conduit::Node res;
  if(mesh_type == "uniform")
  {
    res = detail::get_uniform_vert(n_coords, index);
  }
  else if(mesh_type == "rectilinear")
  {
    res = detail::get_rectilinear_vert(n_coords, index);
  }
  else if(mesh_type == "unstructured" || mesh_type == "structured")
  {
    res = detail::get_explicit_vert(n_coords, index);
  }
  else
  {
    ASCENT_ERROR("The Architect: unknown mesh type: '"<<mesh_type<<"'");
  }

  return res;
}

conduit::Node
element_location(const conduit::Node &domain,
              const int &index,
              const std::string topo_name)
{
  std::string topo = topo_name;
  // if we don't specify a topology, find the first topology ...
  if(topo_name == "")
  {
      conduit::NodeConstIterator itr = domain["topologies"].children();
      itr.next();
      topo = itr.name();
  }

  const conduit::Node &n_topo   = domain["topologies"][topo];
  const std::string mesh_type   = n_topo["type"].as_string();
  const std::string coords_name = n_topo["coordset"].as_string();

  const conduit::Node &n_coords = domain["coordsets"][coords_name];

  conduit::Node res;
  if(mesh_type == "uniform")
  {
    res = detail::get_uniform_element(n_coords, index);
  }
  else if(mesh_type == "rectilinear")
  {
    res = detail::get_rectilinear_element(n_coords, index);
  }
  else if(mesh_type == "unstructured" || mesh_type == "structured")
  {
    res = detail::get_explicit_element(n_coords, n_topo, index);
  }
  else
  {
    ASCENT_ERROR("The Architect: unknown mesh type: '"<<mesh_type<<"'");
  }

  return res;
}

bool is_scalar_field(const conduit::Node &dataset, const std::string &field_name)
{
  bool is_scalar = false;
  bool has_field = false;
  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(!has_field && dom.has_path("fields/"+field_name))
    {
      has_field = true;
      const conduit::Node &n_field = dom["fields/"+field_name];
      const int num_children = n_field["values"].number_of_children();
      if(num_children == 0)
      {
        is_scalar = true;
      }
    }
  }
  return is_scalar;
}

bool has_field(const conduit::Node &dataset, const std::string &field_name)
{
  bool has_field = false;
  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(!has_field && dom.has_path("fields/"+field_name))
    {
      has_field = true;
    }
  }
  // check to see if the field exists in any rank
  has_field = detail::at_least_one(has_field);
  return has_field;
}

bool has_topology(const conduit::Node &dataset, const std::string &topo_name)
{
  bool has_topo = false;
  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(!has_topo && dom.has_path("topologies/"+topo_name))
    {
      has_topo = true;
    }
  }
  // check to see if the field exists in any rank
  has_topo = detail::at_least_one(has_topo);
  return has_topo;
}

conduit::Node
field_histogram(const conduit::Node &dataset,
                const std::string &field,
                const double &min_val,
                const double &max_val,
                const int &num_bins)
{

  double *bins = new double[num_bins];
  memset(bins, 0, sizeof(double) * num_bins);

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field))
    {
      const std::string path = "fields/" + field + "/values";
      conduit::Node res;
      res = array_histogram(dom[path], min_val, max_val, num_bins);

      double *dom_hist = res["value"].value();
#ifdef ASCENT_USE_OPENMP
      #pragma omp parallel for
#endif
      for(int b = 0; b < num_bins; ++b)
      {
        bins[b] += dom_hist[b];
      }
    }
  }
  conduit::Node res;

#ifdef ASCENT_MPI_ENABLED

  double *global_bins = new double[num_bins];

  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Allreduce(bins, global_bins, num_bins, MPI_INT, MPI_SUM, mpi_comm);

  double *tmp = bins;
  bins = global_bins;
  delete[] tmp;
#endif
  res["value"].set(bins, num_bins);
  res["min_val"] = min_val;
  res["max_val"] = max_val;
  res["num_bins"] = num_bins;
  delete[] bins;
  return res;
}

conduit::Node
field_entropy(const conduit::Node &hist)
{
  const double *hist_bins = hist["attrs/value/value"].value();
  const int num_bins = hist["attrs/num_bins/value"].to_int32();
  double sum = array_sum(hist["attrs/value/value"])["value"].to_float64();
  double entropy = 0;

#ifdef ASCENT_USE_OPENMP
      #pragma omp parallel for reduction(+ : entropy)
#endif
  for(int b = 0; b < num_bins; ++b)
  {
    if(hist_bins[b] != 0)
    {
      double p = hist_bins[b] / sum;
      entropy += -p * std::log(p);
    }
  }

  conduit::Node res;
  res["value"] = entropy;
  return res;
}

conduit::Node
field_pdf(const conduit::Node &hist)
{
  const double *hist_bins = hist["attrs/value/value"].value();
  const int num_bins = hist["attrs/num_bins/value"].to_int32();
  double min_val = hist["attrs/min_val/value"].to_float64();
  double max_val = hist["attrs/max_val/value"].to_float64();

  double sum = array_sum(hist["attrs/value/value"])["value"].to_float64();

  double *pdf = new double[num_bins];
  memset(pdf, 0, sizeof(double) * num_bins);

#ifdef ASCENT_USE_OPENMP
      #pragma omp parallel for
#endif
  for(int b = 0; b < num_bins; ++b)
  {
    pdf[b] = hist_bins[b] / sum;
  }

  conduit::Node res;
  res["value"].set(pdf, num_bins);
  res["min_val"] = min_val;
  res["max_val"] = max_val;
  res["num_bins"] = num_bins;
  return res;
}

conduit::Node
field_cdf(const conduit::Node &hist)
{
  const double *hist_bins = hist["attrs/value/value"].value();
  const int num_bins = hist["attrs/num_bins/value"].to_int32();
  double min_val = hist["attrs/min_val/value"].to_float64();
  double max_val = hist["attrs/max_val/value"].to_float64();

  double sum = array_sum(hist["attrs/value/value"])["value"].to_float64();

  double rolling_cdf = 0;

  double *cdf = new double[num_bins];
  memset(cdf, 0, sizeof(double) * num_bins);

  //TODO can this be parallel?
  for(int b = 0; b < num_bins; ++b)
  {
    rolling_cdf += hist_bins[b] / sum;
    cdf[b] = rolling_cdf;
  }

  conduit::Node res;
  res["value"].set(cdf, num_bins);
  res["min_val"] = min_val;
  res["max_val"] = max_val;
  res["num_bins"] = num_bins;
  return res;
}

// this only makes sense on a count histogram
conduit::Node quantile(const conduit::Node &cdf,
                       const double val,
                       const std::string interpolation)
{
  const double *cdf_bins = cdf["attrs/value/value"].value();
  const int num_bins = cdf["attrs/num_bins/value"].to_int32();
  double min_val = cdf["attrs/min_val/value"].to_float64();
  double max_val = cdf["attrs/max_val/value"].to_float64();

  conduit::Node res;

  int bin = 0;

  for(; cdf_bins[bin] < val; ++bin);
  // we overshot
  if(cdf_bins[bin] > val) --bin;
  // i and j are the bin boundaries
  double i = min_val + bin * (max_val - min_val) / num_bins;
  double j = min_val + (bin + 1) * (max_val - min_val) / num_bins;

  if(interpolation == "linear") {
    if(cdf_bins[bin+1] - cdf_bins[bin] == 0)
    {
      res["value"] = i;
    }
    else
    {
      res["value"] = i + (j - i) * (val - cdf_bins[bin]) / (cdf_bins[bin+1] - cdf_bins[bin]);
    }
  }
  else if(interpolation == "lower")
  {
    res["value"] = i;
  }
  else if(interpolation == "higher")
  {
    res["value"] = j;
  }
  else if(interpolation == "midpoint")
  {
    res["value"] = (i + j) / 2;
  }
  else if(interpolation == "nearest")
  {
    res["value"] = (val - i < j - val)? i : j;
  }

  return res;
}

conduit::Node
field_nan_count(const conduit::Node &dataset,
                const std::string &field)
{
  double nan_count = 0;

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field))
    {
      const std::string path = "fields/" + field + "/values";
      conduit::Node res;
      res = array_nan_count(dom[path]);
      nan_count += res["value"].to_float64();
    }
  }
  conduit::Node res;
  res["value"] = nan_count;

  return res;
}

conduit::Node
field_inf_count(const conduit::Node &dataset,
                const std::string &field)
{
  double inf_count = 0;

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field))
    {
      const std::string path = "fields/" + field + "/values";
      conduit::Node res;
      res = array_inf_count(dom[path]);
      inf_count += res["value"].to_float64();
    }
  }
  conduit::Node res;
  res["value"] = inf_count;

  return res;
}

conduit::Node
field_min(const conduit::Node &dataset,
          const std::string &field)
{
  double min_value = std::numeric_limits<double>::max();

  int domain = -1;
  int domain_id = -1;
  int index = -1;

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field))
    {
      const std::string path = "fields/" + field + "/values";
      conduit::Node res;
      res = array_min(dom[path]);
      double a_min = res["value"].to_float64();
      if(a_min < min_value)
      {
        min_value = a_min;
        index = res["index"].as_int32();
        domain = i;
        domain_id = dom["state/domain_id"].to_int32();
      }
    }
  }

  const std::string assoc_str = dataset.child(0)["fields/"
                                + field + "/association"].as_string();

  conduit::Node loc;
  if(assoc_str == "vertex")
  {
    loc = vert_location(dataset.child(domain),index);
  }
  else if(assoc_str == "element")
  {
    loc = element_location(dataset.child(domain),index);
  }
  else
  {
    ASCENT_ERROR("Location for "<<assoc_str<<" not implemented");
  }

  int rank = 0;
  conduit::Node res;
#ifdef ASCENT_MPI_ENABLED
  struct MinLoc
  {
    double value;
    int rank;
  };

  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Comm_rank(mpi_comm, &rank);

  MinLoc minloc = {min_value, rank};
  MinLoc minloc_res;
  MPI_Allreduce( &minloc, &minloc_res, 1, MPI_DOUBLE_INT, MPI_MINLOC, mpi_comm);
  min_value = minloc_res.value;

  double * ploc = loc.as_float64_ptr();
  MPI_Bcast(ploc, 3, MPI_DOUBLE, minloc_res.rank, mpi_comm);
  MPI_Bcast(&domain_id, 1, MPI_INT, minloc_res.rank, mpi_comm);

  loc.set(ploc, 3);

  rank = minloc_res.rank;
#endif
  res["rank"] = rank;
  res["domain_id"] = domain_id;
  res["position"] = loc;
  res["value"] = min_value;

  return res;
}

conduit::Node
field_sum(const conduit::Node &dataset,
          const std::string &field)
{

  double sum = 0.;
  long long int count = 0;

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field))
    {
      const std::string path = "fields/" + field + "/values";
      conduit::Node res;
      res = array_sum(dom[path]);

      double a_sum = res["value"].to_float64();
      long long int a_count = res["count"].to_int64();

      sum += a_sum;
      count += a_count;
    }
  }

#ifdef ASCENT_MPI_ENABLED
  int rank;
  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Comm_rank(mpi_comm, &rank);
  double global_sum;
  MPI_Allreduce(&sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, mpi_comm);

  long long int global_count;
  MPI_Allreduce(&count, &global_count, 1, MPI_LONG_LONG_INT, MPI_SUM, mpi_comm);

  sum = global_sum;
  count = global_count;
#endif

  conduit::Node res;
  res["value"] = sum;
  res["count"] = count;
  return res;
}

conduit::Node
field_avg(const conduit::Node &dataset,
          const std::string &field)
{
  conduit::Node sum = field_sum(dataset, field);

  double avg = sum["value"].to_float64() / sum["count"].to_float64();

  conduit::Node res;
  res["value"] = avg;
  return res;
}

conduit::Node
field_max(const conduit::Node &dataset,
          const std::string &field)
{
  double max_value = std::numeric_limits<double>::lowest();

  int domain = -1;
  int domain_id = -1;
  int index = -1;

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field))
    {
      const std::string path = "fields/" + field + "/values";
      conduit::Node res;
      res = array_max(dom[path]);
      double a_max = res["value"].to_float64();
      if(a_max > max_value)
      {
        max_value = a_max;
        index = res["index"].as_int32();
        domain = i;
        domain_id = dom["state/domain_id"].to_int32();
      }
    }
  }

  const std::string assoc_str = dataset.child(0)["fields/"
                                + field + "/association"].as_string();

  conduit::Node loc;
  if(assoc_str == "vertex")
  {
    loc = vert_location(dataset.child(domain),index);
  }
  else if(assoc_str == "element")
  {
    loc = element_location(dataset.child(domain),index);
  }
  else
  {
    ASCENT_ERROR("Location for "<<assoc_str<<" not implemented");
  }

  int rank = 0;
  conduit::Node res;
#ifdef ASCENT_MPI_ENABLED
  struct MaxLoc
  {
    double value;
    int rank;
  };

  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Comm_rank(mpi_comm, &rank);

  MaxLoc maxloc = {max_value, rank};
  MaxLoc maxloc_res;
  MPI_Allreduce( &maxloc, &maxloc_res, 1, MPI_DOUBLE_INT, MPI_MAXLOC, mpi_comm);
  max_value = maxloc_res.value;

  double * ploc = loc.as_float64_ptr();
  MPI_Bcast(ploc, 3, MPI_DOUBLE, maxloc_res.rank, mpi_comm);
  MPI_Bcast(&domain_id, 1, MPI_INT, maxloc_res.rank, mpi_comm);

  loc.set(ploc, 3);
  rank = maxloc_res.rank;
#endif
  res["rank"] = rank;
  res["domain_id"] = domain_id;
  res["position"] = loc;
  res["value"] = max_value;

  return res;
}

conduit::Node
get_state_var(const conduit::Node &dataset,
              const std::string &var_name)
{
  bool has_state = false;
  conduit::Node state;
  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(!has_state && dom.has_path("state/"+var_name))
    {
      has_state = true;
      state = dom["state/"+var_name];
    }
  }

  // TODO: make sure everyone has this
  if(!has_state)
  {
    ASCENT_ERROR("Unable to retrieve state variable '"<<var_name<<"'");
  }
  return state;
}

std::string field_assoc(const conduit::Node &dataset,
                        const std::string &field_name)
{
  bool vertex = true;
  bool rank_has = false;

  const std::string field_path = "fields/" + field_name;
  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path(field_path))
    {
      rank_has = true;
      std::string asc = dom[field_path+"/association"].as_string();
      if(asc == "element")
      {
        vertex = false;
      }
    }
  }

  bool my_vote = rank_has && vertex;
  bool vertex_vote = global_someone_agrees(my_vote);
  my_vote = rank_has && !vertex;
  bool element_vote = global_someone_agrees(my_vote);

  if(vertex_vote && element_vote)
  {
    ASCENT_ERROR("There is disagreement about the association "
                 <<"of field "<<field_name);
  }

  return vertex_vote ? "vertex" : "element";
}

std::string field_type(const conduit::Node &dataset,
                       const std::string &field_name)
{
  bool is_double = true;
  bool rank_has = false;
  bool error = false;

  const std::string field_path = "fields/" + field_name;
  std::string type_name;
  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path(field_path))
    {
      rank_has = true;
      std::string asc = dom[field_path+"/association"].as_string();
      if(dom[field_path+"/values"].dtype().is_float32())
      {
        is_double = false;
      }
      else if(!dom[field_path+"/values"].dtype().is_float64())
      {
        type_name = dom[field_path+"/values"].dtype().name();
        error = true;
      }
    }
  }

  error = global_agreement(error);
  if(error)
  {

    ASCENT_ERROR("Field '"<<field_name<<"' is neither float or double."
                 <<" type is '"<<type_name<<"'."
                 <<" Contact someone.");
  }

  bool my_vote = rank_has && is_double;
  bool double_vote = global_someone_agrees(my_vote);

  return is_double? "double" : "float";
}

void topology_types(const conduit::Node &dataset,
                    const std::string &topo_name,
                    int topo_types[5])
{

  for(int i = 0; i < 5; ++i)
  {
    topo_types[i] = 0;
  }

  const int num_domains = dataset.number_of_children();
  for(int i = 0; i < num_domains; ++i)
  {
    const conduit::Node &dom = dataset.child(0);
    if(dom.has_path("topologies/"+topo_name))
    {
      const std::string topo_type = dom["topologies/"+topo_name+"/type"].as_string();
      if(topo_type == "points")
      {
        topo_types[0] += 1;
      }
      else if(topo_type == "uniform")
      {
        topo_types[1] += 1;
      }
      else if(topo_type == "rectilinear")
      {
        topo_types[2] += 1;
      }
      else if(topo_type == "structured")
      {
        topo_types[3] += 1;
      }
      else if(topo_type == "unstructured")
      {
        topo_types[4] += 1;
      }
    }
  }

  int res[5];
#ifdef ASCENT_MPI_ENABLED
  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Allreduce(MPI_IN_PLACE,
                topo_types,
                5,
                MPI_INT,
                MPI_SUM,
                mpi_comm);
#endif
}


int num_cells(const conduit::Node &domain, const std::string &topo_name)
{
  const conduit::Node &n_topo = domain["topologies/"+topo_name];
  const std::string topo_type = n_topo["type"].as_string();

  int res = -1;

  if(topo_type == "unstructured")
  {
    const std::string shape = n_topo["elements/shape"].as_string();
    const int conn_size = n_topo["elements/connectiviy"].dtype().number_of_elements();
    const int per_cell = detail::get_num_indices(shape);
    res = conn_size / per_cell;
  }

  if(topo_type == "points")
  {
    return num_points(domain, topo_name);
  }

  const std::string c_name = n_topo["coordset"].as_string();
  const conduit::Node n_coords = domain["coordsets/" + c_name];

  if(topo_type == "uniform")
  {
    res = n_coords["dims/i"].to_int32() - 1;
    if(n_coords.has_path("dims/j"))
    {
      res *= n_coords["dims/j"].to_int32() - 1;
    }
    if(n_coords.has_path("dims/k"))
    {
      res *= n_coords["dims/k"].to_int32() - 1;
    }
  }

  if(topo_type == "rectilinear")
  {
    res = n_coords["values/x"].dtype().number_of_elements() - 1;

    if(n_coords.has_path("values/y"))
    {
      res *= n_coords["values/y"].dtype().number_of_elements() - 1;
    }

    if(n_coords.has_path("values/z"))
    {
      res *= n_coords["values/z"].dtype().number_of_elements() - 1;
    }
  }

  if(topo_type == "explicit")
  {
    res = n_coords["values/x"].dtype().number_of_elements() - 1;
  }

  return res;
}

int num_points(const conduit::Node &domain, const std::string &topo_name)
{
  int res = 0;

  const conduit::Node &n_topo = domain["topologies/"+topo_name];

  const std::string c_name = n_topo["coordset"].as_string();
  const conduit::Node n_coords = domain["coordsets/" + c_name];
  const std::string c_type = n_coords["type"].as_string();

  if(c_type == "uniform")
  {
    res = n_coords["dims/i"].to_int32();
    if(n_coords.has_path("dims/j"))
    {
      res *= n_coords["dims/j"].to_int32();
    }
    if(n_coords.has_path("dims/k"))
    {
      res *= n_coords["dims/k"].to_int32();
    }
  }

  if(c_type == "rectilinear")
  {
    res = n_coords["values/x"].dtype().number_of_elements();

    if(n_coords.has_path("values/y"))
    {
      res *= n_coords["values/y"].dtype().number_of_elements();
    }

    if(n_coords.has_path("values/z"))
    {
      res *= n_coords["values/z"].dtype().number_of_elements();
    }
  }

  if(c_type == "explicit")
  {
    res = n_coords["values/x"].dtype().number_of_elements();
  }

  return res;
}

int spatial_dims(const conduit::Node &dataset, const std::string &topo_name)
{
  const int num_domains = dataset.number_of_children();

  bool is_3d = false;
  bool rank_has = false;

  for(int i = 0; i < num_domains; ++i)
  {
    const conduit::Node &domain = dataset.child(i);
    if(!domain.has_path("topologies/"+topo_name))
    {
      continue;
    }

    rank_has = true;
    const conduit::Node &n_topo = domain["topologies/"+topo_name];

    const std::string c_name = n_topo["coordset"].as_string();
    const conduit::Node n_coords = domain["coordsets/" + c_name];
    const std::string c_type = n_coords["type"].as_string();

    if(c_type == "uniform")
    {
      if(n_coords.has_path("dims/k"))
      {
        is_3d = true;
      }
      break;
    }

    if(c_type == "rectilinear" || c_type == "explicit")
    {
      if(n_coords.has_path("values/z"))
      {
        is_3d = true;
      }
      break;
    }
  }

  bool my_vote = rank_has && is_3d;
  bool vote_3d = global_someone_agrees(my_vote);
  my_vote = rank_has && !is_3d;
  bool vote_2d = global_someone_agrees(my_vote);

  if(vote_2d && vote_3d)
  {
    ASCENT_ERROR("There is disagreement about the spatial dims"
                 <<"of the topoloy '"<<topo_name<<"'");
  }

  return vote_3d ? 3 : 2;

}

std::string
field_topology(const conduit::Node &dataset, const std::string &field_name)
{
  std::string topo_name;
  const int num_domains = dataset.number_of_children();
  for(int i = 0; i < num_domains; ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path("fields/"+field_name))
    {
      topo_name = dom["fields/"+field_name+"/topology"].as_string();
      break;
    }
  }

#if defined(ASCENT_MPI_ENABLED)
  int rank;
  MPI_Comm mpi_comm = MPI_Comm_f2c(flow::Workspace::default_mpi_comm());
  MPI_Comm_rank(mpi_comm, &rank);

  struct MaxLoc
  {
    double size;
    int rank;
  };

  // there is no MPI_INT_INT so shove the "small" size into double
  MaxLoc maxloc = {(double)topo_name.length(), rank};
  MaxLoc maxloc_res;
  MPI_Allreduce( &maxloc, &maxloc_res, 1, MPI_DOUBLE_INT, MPI_MAXLOC, mpi_comm);

  conduit::Node msg;
  msg["topo"] = topo_name;
  conduit::relay::mpi::broadcast_using_schema(msg,maxloc_res.rank,mpi_comm);

  if(!msg["topo"].dtype().is_string())
  {
    ASCENT_ERROR("failed to broadcast topo name");
  }
  topo_name = msg["topo"].as_string();
#endif
  return topo_name;
}

// double or float, checks for global consistency
std::string coord_type(const conduit::Node &dataset,
                       const std::string &topo_name)
{
  // ok, so we  can have a mix of uniform and non-uniform
  // coords, where non-uniform coords have arrays
  // if we only have unirform, the double,
  // if some have arrays, then go with whatever
  // that is.
  bool is_float = false;
  bool has_array = false;
  bool error = false;

  const std::string topo_path = "topology/" + topo_name;
  std::string type_name;

  for(int i = 0; i < dataset.number_of_children(); ++i)
  {
    const conduit::Node &dom = dataset.child(i);
    if(dom.has_path(topo_path))
    {
      std::string coord_name = dom[topo_path+"/coordset"].as_string();
      const conduit::Node &n_coords = dom["coordsets/"+coord_name];
      const std::string coords_type = n_coords["type"].as_string();
      if(coords_type != "uniform")
      {
        has_array = true;

        if(n_coords["values/x"].dtype().is_float32())
        {
          is_float = true;
        }
        else if(!n_coords["values/x"].dtype().is_float64())
        {
          is_float = false;
          type_name = n_coords["/values/x"].dtype().name();
          error = true;
        }
      }
    }
  }

  error = global_agreement(error);

  if(error)
  {

    ASCENT_ERROR("Coords array from topo '"<<topo_name
                 <<"' is neither float or double."
                 <<" type is '"<<type_name<<"'."
                 <<" Contact someone.");
  }

  bool my_vote = has_array && is_float;
  bool float_vote = global_someone_agrees(my_vote);

  return float_vote ? "float" : "double";
}
//-----------------------------------------------------------------------------
};
//-----------------------------------------------------------------------------
// -- end ascent::runtime::expressions--
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
};
//-----------------------------------------------------------------------------
// -- end ascent::runtime --
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
};
//-----------------------------------------------------------------------------
// -- end ascent:: --
//-----------------------------------------------------------------------------





