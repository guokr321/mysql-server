#ifndef SQL_GIS_FUNCTOR_H_INCLUDED
#define SQL_GIS_FUNCTOR_H_INCLUDED
// Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; version 2 of the License.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, 51 Franklin
// Street, Suite 500, Boston, MA 02110-1335 USA.

/// @file
///
/// This file contains the superclasses for GIS functors.
///
/// Each GIS function is split in two: a functor class (for internal use) and a
/// function (for external use) that uses the functor. The functor provides the
/// internal interface to GIS functions, and it may throw exceptions. Some
/// functions may need a combination of different functors to implement the
/// desired functionality.
///
/// The function, not the functor, is the interface to the rest of MySQL.
///
/// @see distance_functor.h

#include <exception>  // std::exception
#include <sstream>    // std::stringstream
#include <string>     // std::string

#include "my_dbug.h"             // DBUG_ASSERT
#include "sql/gis/geometries.h"  // gis::{Geometry{,_type}, Coordinate_system}
#include "sql/gis/geometries_cs.h"  // gis::{Cartesian_*, Geographic_*}
#include "template_utils.h"         // down_cast

namespace gis {

/// Function/parameter combination not implemented exception.
///
/// Geometry is tagged as geographic or Cartesian/projected. In the latter case,
/// whether it is Cartesian or projected is determined by the accompanying SRS.
///
/// To obtain an instance of this exception, use
/// not_implemented_exception::for_projected if geometry is projected, and
/// not_implemented_exception::for_non_projected otherwise.
class not_implemented_exception : public std::exception {
 public:
  enum Srs_type : char { kCartesian, kGeographic, kProjected };

 private:
  Srs_type m_srs_type;

  std::string m_typenames;

  const char *type_to_name(Geometry_type type) const {
    switch (type) {
      case Geometry_type::kPoint:
        return "POINT";
      case Geometry_type::kLinestring:
        return "LINESTRING";
      case Geometry_type::kPolygon:
        return "POLYGON";
      case Geometry_type::kGeometrycollection:
        return "GEOMETRYCOLLECTION";
      case Geometry_type::kMultipoint:
        return "MULTIPOINT";
      case Geometry_type::kMultilinestring:
        return "MULTILINESTRING";
      case Geometry_type::kMultipolygon:
        return "MULTIPOLYGON";
      default:
        DBUG_ASSERT(false); /* purecov: inspected */
        return "UNKNOWN";
    }
  }

  not_implemented_exception(Srs_type srs_type, const Geometry &g) {
    m_srs_type = srs_type;
    m_typenames = std::string(type_to_name(g.type()));
  }

  not_implemented_exception(Srs_type srs_type, const Geometry &g1,
                            const Geometry &g2) {
    DBUG_ASSERT(g1.coordinate_system() == g2.coordinate_system());
    m_srs_type = srs_type;
    std::stringstream ss;
    ss << type_to_name(g1.type()) << ", " << type_to_name(g2.type());
    m_typenames = ss.str();
  }

 public:
  Srs_type srs_type() const { return m_srs_type; }
  const char *typenames() const { return m_typenames.c_str(); }

  static not_implemented_exception for_projected(const Geometry &g) {
    return not_implemented_exception(kProjected, g);
  }

  static not_implemented_exception for_projected(const Geometry &g1,
                                                 const Geometry &g2) {
    return not_implemented_exception(kProjected, g1, g2);
  }

  static not_implemented_exception for_non_projected(const Geometry &g) {
    switch (g.coordinate_system()) {
      default: DBUG_ASSERT(false);  // C++11 woes. /* purecov: inspected */
      case Coordinate_system::kCartesian:
        return not_implemented_exception(kCartesian, g);
      case Coordinate_system::kGeographic:
        return not_implemented_exception(kGeographic, g);
    }
  }

  static not_implemented_exception for_non_projected(const Geometry &g1,
                                                     const Geometry &g2) {
    switch (g1.coordinate_system()) {
      default: DBUG_ASSERT(false);  // C++11 woes. /* purecov: inspected */
      case Coordinate_system::kCartesian:
        return not_implemented_exception(kCartesian, g1, g2);
      case Coordinate_system::kGeographic:
        return not_implemented_exception(kGeographic, g1, g2);
    }
  }
};

/// NULL value exception.
///
/// Thrown when the functor discovers that the result is NULL. Normally, NULL
/// returns can be detected before calling the functor, but not always.
class null_value_exception : public std::exception {};

/// The base class of all functors that takes two geometry arguments.
///
/// Subclasses of this functor base class will implement operator() and call
/// apply() to do type combination dispatching. The actual body of the functor
/// is in the eval() member function, which must be implemented for each
/// different parameter type combination.
///
/// The functor may throw exceptions.
///
/// @tparam T The return type of the functor.
template <typename T>
class Functor {
 public:
  virtual T operator()(const Geometry *g1, const Geometry *g2) const = 0;
  virtual ~Functor() {}

 protected:
  template <typename F>
  static inline T apply(F &f, const Geometry *g1, const Geometry *g2) {
    DBUG_ASSERT(g1->coordinate_system() == g2->coordinate_system());
    switch (g1->coordinate_system()) {
      case Coordinate_system::kCartesian:
        switch (g1->type()) {
          case Geometry_type::kPoint:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Cartesian_point *>(g1),
                              down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Cartesian_point *>(g1),
                              down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Cartesian_point *>(g1),
                              down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_point *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Cartesian_point *>(g1),
                              down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(down_cast<const Cartesian_point *>(g1),
                              down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Cartesian_point *>(g1),
                              down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kLinestring:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Cartesian_linestring *>(g1),
                              down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Cartesian_linestring *>(g1),
                              down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Cartesian_linestring *>(g1),
                              down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_linestring *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Cartesian_linestring *>(g1),
                              down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(down_cast<const Cartesian_linestring *>(g1),
                              down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Cartesian_linestring *>(g1),
                              down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kPolygon:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Cartesian_polygon *>(g1),
                              down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Cartesian_polygon *>(g1),
                              down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Cartesian_polygon *>(g1),
                              down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_polygon *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Cartesian_polygon *>(g1),
                              down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(down_cast<const Cartesian_polygon *>(g1),
                              down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Cartesian_polygon *>(g1),
                              down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kGeometrycollection:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(
                    down_cast<const Cartesian_geometrycollection *>(g1),
                    down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kMultipoint:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Cartesian_multipoint *>(g1),
                              down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Cartesian_multipoint *>(g1),
                              down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Cartesian_multipoint *>(g1),
                              down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_multipoint *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Cartesian_multipoint *>(g1),
                              down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(down_cast<const Cartesian_multipoint *>(g1),
                              down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Cartesian_multipoint *>(g1),
                              down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kMultilinestring:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Cartesian_multilinestring *>(g1),
                              down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Cartesian_multilinestring *>(g1),
                              down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Cartesian_multilinestring *>(g1),
                              down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_multilinestring *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Cartesian_multilinestring *>(g1),
                              down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(down_cast<const Cartesian_multilinestring *>(g1),
                              down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Cartesian_multilinestring *>(g1),
                              down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kMultipolygon:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Cartesian_multipolygon *>(g1),
                              down_cast<const Cartesian_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Cartesian_multipolygon *>(g1),
                              down_cast<const Cartesian_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Cartesian_multipolygon *>(g1),
                              down_cast<const Cartesian_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Cartesian_multipolygon *>(g1),
                    down_cast<const Cartesian_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Cartesian_multipolygon *>(g1),
                              down_cast<const Cartesian_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(down_cast<const Cartesian_multipolygon *>(g1),
                              down_cast<const Cartesian_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Cartesian_multipolygon *>(g1),
                              down_cast<const Cartesian_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kGeometry:
            DBUG_ASSERT(false); /* purecov: inspected */
            throw not_implemented_exception::for_non_projected(*g1, *g2);
        }  // switch (g1->type())
      case Coordinate_system::kGeographic:
        switch (g1->type()) {
          case Geometry_type::kPoint:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Geographic_point *>(g1),
                              down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Geographic_point *>(g1),
                              down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Geographic_point *>(g1),
                              down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_point *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Geographic_point *>(g1),
                              down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_point *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Geographic_point *>(g1),
                              down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kLinestring:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Geographic_linestring *>(g1),
                              down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Geographic_linestring *>(g1),
                              down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Geographic_linestring *>(g1),
                              down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_linestring *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Geographic_linestring *>(g1),
                              down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_linestring *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Geographic_linestring *>(g1),
                              down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kPolygon:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Geographic_polygon *>(g1),
                              down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Geographic_polygon *>(g1),
                              down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Geographic_polygon *>(g1),
                              down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_polygon *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Geographic_polygon *>(g1),
                              down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_polygon *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Geographic_polygon *>(g1),
                              down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kGeometrycollection:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(
                    down_cast<const Geographic_geometrycollection *>(g1),
                    down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kMultipoint:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Geographic_multipoint *>(g1),
                              down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Geographic_multipoint *>(g1),
                              down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Geographic_multipoint *>(g1),
                              down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_multipoint *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Geographic_multipoint *>(g1),
                              down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_multipoint *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Geographic_multipoint *>(g1),
                              down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kMultilinestring:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Geographic_multilinestring *>(g1),
                              down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Geographic_multilinestring *>(g1),
                              down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Geographic_multilinestring *>(g1),
                              down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_multilinestring *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Geographic_multilinestring *>(g1),
                              down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_multilinestring *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Geographic_multilinestring *>(g1),
                              down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kMultipolygon:
            switch (g2->type()) {
              case Geometry_type::kPoint:
                return f.eval(down_cast<const Geographic_multipolygon *>(g1),
                              down_cast<const Geographic_point *>(g2));
              case Geometry_type::kLinestring:
                return f.eval(down_cast<const Geographic_multipolygon *>(g1),
                              down_cast<const Geographic_linestring *>(g2));
              case Geometry_type::kPolygon:
                return f.eval(down_cast<const Geographic_multipolygon *>(g1),
                              down_cast<const Geographic_polygon *>(g2));
              case Geometry_type::kGeometrycollection:
                return f.eval(
                    down_cast<const Geographic_multipolygon *>(g1),
                    down_cast<const Geographic_geometrycollection *>(g2));
              case Geometry_type::kMultipoint:
                return f.eval(down_cast<const Geographic_multipolygon *>(g1),
                              down_cast<const Geographic_multipoint *>(g2));
              case Geometry_type::kMultilinestring:
                return f.eval(
                    down_cast<const Geographic_multipolygon *>(g1),
                    down_cast<const Geographic_multilinestring *>(g2));
              case Geometry_type::kMultipolygon:
                return f.eval(down_cast<const Geographic_multipolygon *>(g1),
                              down_cast<const Geographic_multipolygon *>(g2));
              case Geometry_type::kGeometry:
                DBUG_ASSERT(false); /* purecov: inspected */
                throw not_implemented_exception::for_non_projected(*g1, *g2);
            }
          case Geometry_type::kGeometry:
            DBUG_ASSERT(false); /* purecov: inspected */
            throw not_implemented_exception::for_non_projected(*g1, *g2);
        }  // switch (g1->type())
    }      // switch (g1->coordinate_system())

    DBUG_ASSERT(false); /* purecov: inspected */
    throw not_implemented_exception::for_non_projected(*g1, *g2);
  }
};

/// The base class of all functors that take one geometry argument.
///
/// Subclasses of this functor base class will implement operator() and call
/// apply() to do type combination dispatching. The actual body of the functor
/// is in the eval() member function, which must be implemented for each
/// different parameter type combination.
///
/// The functor may throw exceptions.
///
/// @tparam T The return type of the functor.
template <typename T>
class Unary_functor {
 public:
  virtual T operator()(const Geometry &) const = 0;
  virtual ~Unary_functor() {}

 protected:
  template <class F>
  static inline T apply(F &f, const Geometry &g) {
    switch (g.coordinate_system()) {
      case Coordinate_system::kCartesian: {
        switch (g.type()) {
          case Geometry_type::kPoint:
            return f.eval(down_cast<const Cartesian_point &>(g));
          case Geometry_type::kLinestring:
            return f.eval(down_cast<const Cartesian_linestring &>(g));
          case Geometry_type::kPolygon:
            return f.eval(down_cast<const Cartesian_polygon &>(g));
          case Geometry_type::kGeometrycollection:
            return f.eval(down_cast<const Cartesian_geometrycollection &>(g));
          case Geometry_type::kMultipoint:
            return f.eval(down_cast<const Cartesian_multipoint &>(g));
          case Geometry_type::kMultilinestring:
            return f.eval(down_cast<const Cartesian_multilinestring &>(g));
          case Geometry_type::kMultipolygon:
            return f.eval(down_cast<const Cartesian_multipolygon &>(g));
          case Geometry_type::kGeometry:
            DBUG_ASSERT(false); /* purecov: inspected */
            // We don't know here whether the geometry is Cartesan or projected.
            // Assume Cartesian. This is dead code anyway.
            throw not_implemented_exception::for_non_projected(g);
        }
      }
      case Coordinate_system::kGeographic: {
        switch (g.type()) {
          case Geometry_type::kPoint:
            return f.eval(down_cast<const Geographic_point &>(g));
          case Geometry_type::kLinestring:
            return f.eval(down_cast<const Geographic_linestring &>(g));
          case Geometry_type::kPolygon:
            return f.eval(down_cast<const Geographic_polygon &>(g));
          case Geometry_type::kGeometrycollection:
            return f.eval(down_cast<const Geographic_geometrycollection &>(g));
          case Geometry_type::kMultipoint:
            return f.eval(down_cast<const Geographic_multipoint &>(g));
          case Geometry_type::kMultilinestring:
            return f.eval(down_cast<const Geographic_multilinestring &>(g));
          case Geometry_type::kMultipolygon:
            return f.eval(down_cast<const Geographic_multipolygon &>(g));
          case Geometry_type::kGeometry:
            DBUG_ASSERT(false); /* purecov: inspected */
            // We don't know here whether the geometry is Cartesan or projected.
            // Assume Cartesian. This is dead code anyway.
            throw not_implemented_exception::for_non_projected(g);
        }
      }
    }
    DBUG_ASSERT(false); /* purecov: inspected */
    // We don't know here whether the geometry is Cartesan or projected.
    // Assume Cartesian. This is dead code anyway.
    throw not_implemented_exception::for_non_projected(g);
  }
};

}  // namespace gis

#endif  // SQL_GIS_FUNCTOR_H_INCLUDED
