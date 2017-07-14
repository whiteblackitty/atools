/*****************************************************************************
* Copyright 2015-2017 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "fs/xp/xpconstants.h"

#include "fs/bgl/ap/rw/runway.h"

namespace atools {
namespace fs {
namespace xp {

QString surfaceToDb(Surface value)
{
  switch(value)
  {
    case atools::fs::xp::UNKNOWN:
      return "UNKNOWN";

    case atools::fs::xp::TRANSPARENT:
      return "TR";

    case atools::fs::xp::ASPHALT:
      return "A";

    case atools::fs::xp::CONCRETE:
      return "C";

    case atools::fs::xp::TURF_OR_GRASS:
      return "G";

    case atools::fs::xp::DRY_LAKEBED:
    case atools::fs::xp::DIRT:
      return "D";

    case atools::fs::xp::GRAVEL:
      return "GR";

    case atools::fs::xp::WATER:
      return "W";

    case atools::fs::xp::SNOW_OR_ICE:
      return "SN";
  }
  return QString();
}

bool isSurfaceHard(Surface value)
{
  return value == UNKNOWN || value == TRANSPARENT || value == ASPHALT || value == CONCRETE;
}

bool isSurfaceSoft(Surface value)
{
  return value == TURF_OR_GRASS || value == DRY_LAKEBED || value == DIRT || value == GRAVEL || value == SNOW_OR_ICE;
}

bool isSurfaceWater(Surface value)
{
  return value == WATER;
}

int markingToDb(Marking value)
{
  // EDGES = 1 << 0,
  // THRESHOLD = 1 << 1,
  // FIXED_DISTANCE = 1 << 2,
  // TOUCHDOWN = 1 << 3,
  // DASHES = 1 << 4,
  // IDENT = 1 << 5,
  // PRECISION = 1 << 6,
  // EDGE_PAVEMENT = 1 << 7,
  // SINGLE_END = 1 << 8,
  // PRIMARY_CLOSED = 1 << 9,
  // SECONDARY_CLOSED = 1 << 10,
  // PRIMARY_STOL = 1 << 11,
  // SECONDARY_STOL = 1 << 12,
  // ALTERNATE_THRESHOLD = 1 << 13,
  // ALTERNATE_FIXEDDISTANCE = 1 << 14,
  // ALTERNATE_TOUCHDOWN = 1 << 15,
  // ALTERNATE_PRECISION = 1 << 21,
  // LEADING_ZERO_IDENT = 1 << 22,
  // NO_THRESHOLD_END_ARROWS = 1 << 23

  using namespace atools::fs::bgl::rw;

  RunwayMarkingFlags flags(NO_FLAGS);

  switch(value)
  {
    case atools::fs::xp::NO_MARKING:
      break;

    case atools::fs::xp::VISUAL:
      flags = EDGES | DASHES | IDENT;
      break;

    case atools::fs::xp::NON_PAP:
      flags = EDGES | THRESHOLD | FIXED_DISTANCE | TOUCHDOWN | DASHES | IDENT | EDGE_PAVEMENT;
      break;

    case atools::fs::xp::PAP:
      flags = EDGES | THRESHOLD | FIXED_DISTANCE | TOUCHDOWN | DASHES | IDENT | PRECISION | EDGE_PAVEMENT;
      break;

    case atools::fs::xp::UK_NON_PAP:
      flags = EDGES | ALTERNATE_THRESHOLD | ALTERNATE_FIXEDDISTANCE | ALTERNATE_TOUCHDOWN | DASHES | IDENT |
              EDGE_PAVEMENT;
      break;
    case atools::fs::xp::UK_PAP:
      flags = EDGES | ALTERNATE_THRESHOLD | ALTERNATE_FIXEDDISTANCE | ALTERNATE_TOUCHDOWN | DASHES | IDENT |
              ALTERNATE_PRECISION | EDGE_PAVEMENT;
      break;

  }
  return static_cast<int>(flags);
}

QString alsToDb(ApproachLight value)
{
  switch(value)
  {
    case atools::fs::xp::NO_ALS:
      return QString();

    case atools::fs::xp::ALSF_I:
      return "ALSF1";

    case atools::fs::xp::ALSF_II:
      return "ALSF2";

    case atools::fs::xp::CALVERT:
      return "CALVERT";

    case atools::fs::xp::CALVERT_ILS:
      return "CALVERT2";

    case atools::fs::xp::SSALR:
      return "SSALR";

    case atools::fs::xp::SSALF:
      return "SSALF";

    case atools::fs::xp::SALS:
      return "SALS";

    case atools::fs::xp::MALSR:
      return "MALSR";

    case atools::fs::xp::MALSF:
      return "MALSF";

    case atools::fs::xp::MALS:
      return "MALS";

    case atools::fs::xp::ODALS:
      return "ODALS";

    case atools::fs::xp::RAIL:
      return "RAIL";

  }
  return QString();
}

QString approachIndicatorToDb(ApproachIndicator value)
{
  // return "VASI21";
  // return "VASI22";
  // return "VASI31";
  // return "VASI32";
  // return "VASI23";
  // return "VASI33";
  // return "PAPI2";
  // return "PAPI4";
  // return "PVASI";
  // return "TVASI";
  // return "TRICOLOR";
  // return "BALL";
  // return "APAP_PANELS";

  switch(value)
  {
    case atools::fs::xp::VASI:
      return "VASI22";

    case atools::fs::xp::PAPI_4L:
      return "PAPI4";

    case atools::fs::xp::PAPI_4R:
      return "PAPI4";

    case atools::fs::xp::SPACE_SHUTTLE_PAPI:
      return "PAPI4";

    case atools::fs::xp::TRI_COLOR_VASI:
      return "TRICOLOR";

    case atools::fs::xp::RUNWAY_GUARD:
      return "GUARD";

    case atools::fs::xp::NO_APPR_INDICATOR:
      break;
  }
  return QString();
}

} // namespace xp
} // namespace fs
} // namespace atools
