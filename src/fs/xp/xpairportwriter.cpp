/*****************************************************************************
* Copyright 2015-2018 Alexander Barthel albar965@mailbox.org
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

#include "fs/xp/xpairportwriter.h"

#include "sql/sqlutil.h"
#include "geo/calculations.h"
#include "fs/util/fsutil.h"
#include "fs/progresshandler.h"
#include "fs/common/airportindex.h"
#include "fs/common/magdecreader.h"

#include <QDebug>
#include <QRegularExpression>

using atools::sql::SqlQuery;
using atools::sql::SqlUtil;
using atools::sql::SqlRecord;
using atools::geo::Pos;
using atools::geo::Rect;
using atools::geo::meterToFeet;
namespace x = atools::fs::xp;

namespace atools {
namespace fs {
namespace xp {

namespace ap {
enum AirportFieldIndex
{
  ROWCODE = 0,
  ELEVATION = 1,
  // 2x unused
  ICAO = 4,
  NAME = 5
};

}

namespace vp {
enum ViewpointFieldIndex
{
  ROWCODE = 0,
  LATY = 1,
  LONX = 2,
  HEIGHT = 3,
};

}

namespace n {
enum NodeFieldIndex
{
  ROWCODE = 0,
  LATY = 1,
  LONX = 2,
  CTRL_LATY = 3,
  CTRL_LONX = 4
};

}

namespace p {
enum PavementFieldIndex
{
  ROWCODE = 0,
  SURFACE = 1,
  SMOOTHNESS = 2,
  ORIENT = 3,
  DESCRIPTION = 4
};

}

namespace tn {
enum TaxiNodeFieldIndex
{
  ROWCODE = 0,
  LATY = 1,
  LONX = 2,
  USAGE = 3,
  ID = 4,
  NAME = 5
};

}

namespace te {
enum TaxiEdgeFieldIndex
{
  ROWCODE = 0,
  START = 1,
  END = 2,
  DIR = 3,
  TYPE = 4,
  NAME = 5
};

}

namespace s {
enum StartFieldIndex
{
  ROWCODE = 0,
  LATY = 1,
  LONX = 2,
  HEADING = 3,
  NAME = 4
};

}

namespace v {
enum VasiFieldIndex
{
  ROWCODE = 0,
  LATY = 1,
  LONX = 2,
  TYPE = 3,
  ORIENT = 4,
  ANGLE = 5,
  RUNWAY = 6,
  DESCRIPTION = 7
};

}

namespace sl {
enum StartupLocationFieldIndex
{
  ROWCODE = 0,
  LATY = 1,
  LONX = 2,
  HEADING = 3,
  TYPE = 4, // gate, hangar, misc or tie-down
  AIRPLANE_TYPE = 5, // Pipe-separated list (|). heavy, jets, turboprops, props and helos (or just all for all types)
  NAME = 6,
};

}

namespace sm {
enum StartupLocationMetaFieldIndex
{
  ROWCODE = 0,
  WIDTH = 1,
  OPTYPE = 2,
  AIRLINE = 3
};

}

namespace m {
enum MetaFieldIndex
{
  ROWCODE = 0,
  KEY = 1,
  VALUE = 2
};

}
namespace com {
enum ComFieldIndex
{
  ROWCODE = 0,
  FREQUENCY = 1,
  NAME = 2
};

}

namespace hp {
enum HelipadFieldIndex
{
  ROWCODE = 0,
  DESIGNATOR = 1,
  LATY = 2,
  LONX = 3,
  ORIENTATION = 4,
  LENGTH = 5,
  WIDTH = 6,
  SURFACE = 7
};

}

namespace rw {
enum RunwayFieldIndex
{
  ROWCODE = 0,
  WIDTH = 1,

  // Indexes for water runways
  WATER_PRIMARY_NUMBER = 3,
  WATER_PRIMARY_LATY = 4,
  WATER_PRIMARY_LONX = 5,
  WATER_SECONDARY_NUMBER = 6,
  WATER_SECONDARY_LATY = 7,
  WATER_SECONDARY_LONX = 8,

  SURFACE = 2,
  SHOULDER_SURFACE = 3,
  SMOOTHNESS = 4,
  CENTER_LIGHTS = 5,
  EDGE_LIGHTS = 6,
  DISTANCE_REMAINING_SIGNS = 7,

  // Indexes for normal runways
  PRIMARY_NUMBER = 8,
  PRIMARY_LATY = 9,
  PRIMARY_LONX = 10,
  PRIMARY_DISPLACED_THRESHOLD = 11,
  PRIMARY_OVERRUN_BLASTPAD = 12,
  PRIMARY_MARKINGS = 13,
  PRIMARY_ALS = 14,
  PRIMARY_TDZ_LIGHT = 15,
  PRIMARY_REIL = 16,
  SECONDARY_NUMBER = 17,
  SECONDARY_LATY = 18,
  SECONDARY_LONX = 19,
  SECONDARY_DISPLACED_THRESHOLD = 20,
  SECONDARY_OVERRUN_BLASTPAD = 21,
  SECONDARY_MARKINGS = 22,
  SECONDARY_ALS = 23,
  SECONDARY_TDZ_LIGHT = 24,
  SECONDARY_REIL = 25,
};

}

// Remove square brackets from name
const static QRegularExpression NAME_INDICATOR("\\[(h|s|g|x|mil)\\]", QRegularExpression::CaseInsensitiveOption);

XpAirportWriter::XpAirportWriter(atools::sql::SqlDatabase& sqlDb, atools::fs::common::AirportIndex *airportIndexParam,
                                 const NavDatabaseOptions& opts, ProgressHandler *progressHandler,
                                 NavDatabaseErrors *navdatabaseErrors)
  : XpWriter(sqlDb, opts, progressHandler, navdatabaseErrors),
  runwayEndRecord(sqlDb.record("runway_end", ":")), airportIndex(airportIndexParam)
{
  initQueries();
}

XpAirportWriter::~XpAirportWriter()
{
  deInitQueries();
}

void XpAirportWriter::write(const QStringList& line, const XpWriterContext& context)
{
  ctx = &context;
  AirportRowCode rowCode = static_cast<AirportRowCode>(at(line, ap::ROWCODE).toInt());

  if(!contains(rowCode, {x::PAVEMENT_HEADER, x::NODE, x::NODE_AND_CONTROL_POINT,
                         x::NODE_CLOSE, x::NODE_AND_CONTROL_POINT_CLOSE}))
    finishPavement(context);

  if(rowCode != x::RAMP_START_METADATA)
    finishStartupLocation();

  switch(rowCode)
  {
    // Airport header
    case x::LAND_AIRPORT_HEADER:
    case x::SEAPLANE_BASE_HEADER:
    case x::HELIPORT_HEADER:
      finishAirport(context);
      bindAirport(line, rowCode, context);
      break;

    case x::LAND_RUNWAY:
    case x::WATER_RUNWAY:
      bindRunway(line, rowCode, context);
      break;

    case x::HELIPAD:
      writeHelipad(line, context);
      break;

    case x::PAVEMENT_HEADER:
      finishPavement(context);
      bindPavement(line, context);
      break;

    case x::NODE:
    case x::NODE_AND_CONTROL_POINT:
    case x::NODE_CLOSE:
    case x::NODE_AND_CONTROL_POINT_CLOSE:
      bindPavementNode(line, rowCode, context);
      break;

    case x::AIRPORT_VIEWPOINT:
      bindViewpoint(line, context);
      break;

    case x::AEROPLANE_STARTUP_LOCATION:
      writeStartup(line, context);
      break;

    case x::LIGHTING_OBJECT:
      bindVasi(line, context);
      break;

    case x::AIRPORT_LOCATION:
      finishStartupLocation();
      writeStartupLocation(line, context);
      break;

    case x::RAMP_START_METADATA:
      writeStartupLocationMetadata(line, context);
      break;

    case x::TAXI_ROUTE_NETWORK_NODE:
      bindTaxiNode(line, context);
      break;
    case x::TAXI_ROUTE_NETWORK_EDGE:
      bindTaxiEdge(line, context);
      break;

    case x::METADATA_RECORDS:
      bindMetadata(line, context);
      break;

    case x::TRUCK_PARKING_LOCATION:
    case x::TRUCK_DESTINATION_LOCATION:
      bindFuel(line, context);
      break;

    case x::COM_WEATHER:
    case x::COM_UNICOM:
    case x::COM_CLEARANCE:
    case x::COM_GROUND:
    case x::COM_TOWER:
    case x::COM_APPROACH:
    case x::COM_DEPARTURE:
      writeCom(line, rowCode, context);
      break;

    // Unused rowcodes
    case x::LINEAR_FEATURE_HEADER:
    case x::AIRPORT_BOUNDARY_HEADER:
    case x::NODE_TERMINATING_A_STRING:
    case x::NODE_WITH_BEZIER_CONTROL_POINT_NO_CLOSE:
    case x::AIRPORT_LIGHT_BEACON:
    case x::WINDSOCK:
    case x::TAXIWAY_SIGN:
    case x::AIRPORT_TRAFFIC_FLOW:
    case x::TRAFFIC_FLOW_WIND_RULE:
    case x::TRAFFIC_FLOW_MINIMUM_CEILING_RULE:
    case x::TRAFFIC_FLOW_MINIMUM_VISIBILITY_RULE:
    case x::TRAFFIC_FLOW_TIME_RULE:
    case x::RUNWAY_IN_USE:
    case x::VFR_TRAFFIC_PATTERN:
    case x::HEADER_INDICATING_THAT_TAXI_ROUTE_NETWORK_DATA_FOLLOWS:
    case x::TAXI_ROUTE_EDGE_ACTIVE_ZONE:
    case x::NO_ROWCODE:
      break;
  }
}

void XpAirportWriter::finish(const XpWriterContext& context)
{
  finishPavement(context);
  finishStartupLocation();
  finishAirport(context);
}

void XpAirportWriter::bindTaxiNode(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindTaxiNode";

  taxiNodes.insert(at(line, tn::ID).toInt(), Pos(at(line, tn::LONX).toFloat(), at(line, tn::LATY).toFloat()));
}

void XpAirportWriter::bindTaxiEdge(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindTaxiEdge";

  // Ignore runway lines
  if(at(line, te::TYPE) == "runway")
    return;

  const Pos& start = taxiNodes.value(at(line, te::START).toInt());
  const Pos& end = taxiNodes.value(at(line, te::END).toInt());
  airportRect.extend(start);
  airportRect.extend(end);

  QString name = line.size() > te::NAME ? at(line, te::NAME).simplified() : QString();

  // Filter out the various garbage names
  QString nameCompare = name.toUpper();
  if(nameCompare == QLatin1Literal("*") ||
     nameCompare == QLatin1Literal("**") ||
     nameCompare == QLatin1Literal("+") ||
     nameCompare == QLatin1Literal("-") ||
     nameCompare == QLatin1Literal(".") ||
     nameCompare == QLatin1Literal("TAXIWAY") ||
     nameCompare == QLatin1Literal("TAXI_TO_RAMP") ||
     nameCompare == QLatin1Literal("TAXI_RAMP") ||
     nameCompare == QLatin1Literal("TAXY_RAMP") ||
     nameCompare == QLatin1Literal("UNNAMED") ||
     nameCompare == QLatin1Literal("TWY") ||
     nameCompare == QLatin1Literal("TAXI"))
    name.clear();

  numTaxiPath++;
  insertTaxiQuery->bindValue(":taxi_path_id", ++curTaxiPathId);
  insertTaxiQuery->bindValue(":airport_id", curAirportId);
  insertTaxiQuery->bindValue(":surface", QVariant(QVariant::String));
  insertTaxiQuery->bindValue(":width", 0.f);
  insertTaxiQuery->bindValue(":name", name);
  insertTaxiQuery->bindValue(":type", "T" /* taxi */);
  insertTaxiQuery->bindValue(":is_draw_surface", 1);
  insertTaxiQuery->bindValue(":is_draw_detail", 1);

  insertTaxiQuery->bindValue(":has_centerline", 0);
  insertTaxiQuery->bindValue(":has_centerline_light", 0);
  insertTaxiQuery->bindValue(":has_left_edge_light", 0);
  insertTaxiQuery->bindValue(":has_right_edge_light", 0);

  insertTaxiQuery->bindValue(":start_type", "N" /* Normal */);
  insertTaxiQuery->bindValue(":start_lonx", start.getLonX());
  insertTaxiQuery->bindValue(":start_laty", start.getLatY());

  insertTaxiQuery->bindValue(":end_type", "N" /* Normal */);
  insertTaxiQuery->bindValue(":end_lonx", end.getLonX());
  insertTaxiQuery->bindValue(":end_laty", end.getLatY());

  insertTaxiQuery->exec();
}

void XpAirportWriter::bindPavement(const QStringList& line, const XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindPavement";

  currentPavement.clear();
  writingPavementBoundary = true;
  writingPavementHoles = false;
  writingPavementNewHole = false;

  // Start an apron record
  numApron++;
  insertApronQuery->bindValue(":apron_id", ++curApronId);
  insertApronQuery->bindValue(":airport_id", curAirportId);
  insertApronQuery->bindValue(":is_draw_surface", 1);
  insertApronQuery->bindValue(":is_draw_detail", 1);
  insertApronQuery->bindValue(":surface", surfaceToDb(static_cast<Surface>(at(line, p::SURFACE).toInt()), &context));
}

void XpAirportWriter::bindPavementNode(const QStringList& line, atools::fs::xp::AirportRowCode rowCode,
                                       const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindPavementNode";

  Pos node(at(line, n::LONX).toFloat(), at(line, n::LATY).toFloat());
  Pos control;
  airportRect.extend(node);

  if(rowCode == x::NODE_AND_CONTROL_POINT || rowCode == x::NODE_AND_CONTROL_POINT_CLOSE)
    // Bezier cubic or quad control point
    control = Pos(at(line, n::CTRL_LONX).toFloat(), at(line, n::CTRL_LATY).toFloat());

  if(writingPavementBoundary)
    currentPavement.addBoundaryNode(node, control);
  else if(writingPavementHoles)
    currentPavement.addHoleNode(node, control, writingPavementNewHole);

  writingPavementNewHole = false;

  if(rowCode == x::NODE_CLOSE || rowCode == x::NODE_AND_CONTROL_POINT_CLOSE)
  {
    // Last node closing
    if(writingPavementBoundary)
    {
      writingPavementBoundary = false;
      writingPavementHoles = true;
    }

    if(writingPavementHoles)
      writingPavementNewHole = true;
  }
}

void XpAirportWriter::finishPavement(const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(writingPavementBoundary || writingPavementHoles)
  {
    if(!writingAirport)
      qWarning() << context.messagePrefix() << "Invalid writing airport state in finishPavement";

    insertApronQuery->bindValue(":geometry", currentPavement.writeToByteArray());
    insertApronQuery->exec();
    writingPavementBoundary = false;
    writingPavementHoles = false;
    writingPavementNewHole = false;
  }
}

void XpAirportWriter::bindVasi(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindVasi";

  ApproachIndicator type = static_cast<ApproachIndicator>(at(line, v::TYPE).toInt());
  if(type == NO_APPR_INDICATOR || type == RUNWAY_GUARD)
    return;

  // Find runway by name - does not exist in some 850 airport files
  SqlRecord *bestRunwayEnd = nullptr;
  QString rwName = line.value(v::RUNWAY);
  float orientation = at(line, v::ORIENT).toFloat();

  if(!rwName.isEmpty())
  {
    // Try to find a runway record by name
    for(SqlRecord& rec : runwayEndRecords)
    {
      if(rec.valueStr(":name") == rwName)
      {
        bestRunwayEnd = &rec;
        break;
      }
    }
  }

  if(bestRunwayEnd == nullptr)
  {
    // Try to find by angle
    float bestAngle = std::numeric_limits<float>::max() / 4.f;

    for(SqlRecord& rec : runwayEndRecords)
    {
      // Do simple comparison - this will not catch any differences like 355 to 5 degrees
      float diff = std::abs(rec.valueFloat(":heading") - orientation);
      if(diff < 10.f && diff < std::abs(bestAngle - orientation))
      {
        // Smaller angle difference
        bestRunwayEnd = &rec;
        bestAngle = rec.valueFloat(":heading");
      }
    }
  }

  if(bestRunwayEnd != nullptr)
  {
    numRunwayEndVasi++;
    bestRunwayEnd->setValue(":left_vasi_type", approachIndicatorToDb(type, &context));
    bestRunwayEnd->setValue(":left_vasi_pitch", at(line, v::ANGLE).toFloat());
    bestRunwayEnd->setValue(":right_vasi_type", "UNKN");
    bestRunwayEnd->setValue(":right_vasi_pitch", 0.f);
  }
  else
    qWarning() << context.messagePrefix() << "No runway end" << rwName
               << "for VASI with orientation" << orientation << "found";
}

void XpAirportWriter::bindViewpoint(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindViewpoint";

  Pos pos(at(line, vp::LONX).toFloat(), at(line, vp::LATY).toFloat());
  airportRect.extend(pos);
  insertAirportQuery->bindValue(":tower_laty", pos.getLatY());
  insertAirportQuery->bindValue(":tower_lonx", pos.getLonX());
  insertAirportQuery->bindValue(":tower_altitude", airportAltitude + at(line, vp::HEIGHT).toFloat());
  insertAirportQuery->bindValue(":has_tower_object", 1);
  hasTower = true;
}

void XpAirportWriter::writeStartupLocation(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in writeStartupLocation";

  writingStartLocation = true;
  numParking++;
  insertParkingQuery->bindValue(":parking_id", ++curParkingId);
  insertParkingQuery->bindValue(":airport_id", curAirportId);

  Pos pos(at(line, sl::LONX).toFloat(), at(line, sl::LATY).toFloat());
  airportRect.extend(pos);
  insertParkingQuery->bindValue(":laty", pos.getLatY());
  insertParkingQuery->bindValue(":lonx", pos.getLonX());

  insertParkingQuery->bindValue(":heading", at(line, sl::HEADING).toFloat());
  insertParkingQuery->bindValue(":number", -1);
  insertParkingQuery->bindValue(":radius", 50.f);
  // Fill airline codes later from metadata
  insertParkingQuery->bindValue(":airline_codes", QVariant(QVariant::String));

  QString name = mid(line, sl::NAME, true /* ignore error */);

  bool hasFuel = false;
  if(name.toLower().contains("avgas") || name.toLower().contains("mogas") || name.toLower().contains("gas-station"))
  {
    hasFuel = true;
    insertAirportQuery->bindValue(":has_avgas", 1);
  }

  if(name.toLower().contains("jetfuel"))
  {
    hasFuel = true;
    insertAirportQuery->bindValue(":has_jetfuel", 1);
  }

  if(name.toLower().contains("fuel"))
  {
    hasFuel = true;
    insertAirportQuery->bindValue(":has_jetfuel", 1);
    insertAirportQuery->bindValue(":has_avgas", 1);
  }

  insertParkingQuery->bindValue(":name", name);
  insertParkingQuery->bindValue(":has_jetway", 0);

  if(hasFuel)
    insertParkingQuery->bindValue(":type", "FUEL");
  else
  {
    QString type = at(line, sl::TYPE);
    if(type == "gate")
      insertParkingQuery->bindValue(":type", "G");
    else if(type == "hangar")
      insertParkingQuery->bindValue(":type", "H");
    else if(type == "tie-down")
      insertParkingQuery->bindValue(":type", "T");
    // else if(type == "misc")

    // Need at least an empty string bound
    insertParkingQuery->bindValue(":type", "");
  }

  // has_jetway integer not null,     -- 1 if the parking has a jetway attached

  // Airplane types that can use this location
  // Pipe-separated list (|). Can include heavy, jets,
  // turboprops, props and helos (or just all for all types)
}

void XpAirportWriter::writeStartupLocationMetadata(const QStringList& line,
                                                   const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in writeStartupLocation";

  // Operation type none, general_aviation, airline, cargo, military
  // Airline permitted to use this ramp 3-letter airline codes (AAL, SWA, etc)

  bool isFuel = insertParkingQuery->boundValue(":type", true /* ignore invalid */).toString() == "FUEL";
  if(!isFuel)
  {
    // Build type from operations type - not in 850
    QString ops = line.value(sm::OPTYPE);
    if(ops == "general_aviation")
      insertParkingQuery->bindValue(":type", "RGA"); // Ramp GA
    else if(ops == "cargo")
      insertParkingQuery->bindValue(":type", "RC"); // Ramp cargo
    else if(ops == "military")
      insertParkingQuery->bindValue(":type", "RM"); // Ramp military
    // else if(ops == "airline")
    // else if(ops == "none")
  }

  if(line.size() > sm::AIRLINE)
    // Not in 850
    insertParkingQuery->bindValue(":airline_codes", line.value(sm::AIRLINE).toUpper());

  QString sizeType("S");
  float radius = 10.f;

  // ICAO width code A 15 m, B 25 m, C 35 m, D 50 m, E 65 m, F 80 m
  // TODO size type is not clear - not in 850
  QString widthCode = line.value(sm::WIDTH);
  if(widthCode == "A")
  {
    radius = 25.f;
    sizeType = "S";
  }
  else if(widthCode == "B")
  {
    radius = 40.f;
    sizeType = "S";
  }
  else if(widthCode == "C")
  {
    radius = 60.f;
    sizeType = "M";
  }
  else if(widthCode == "D")
  {
    radius = 80.f;
    sizeType = "M";
  }
  else if(widthCode == "E")
  {
    radius = 100.f;
    sizeType = "H";
  }
  else if(widthCode == "F")
  {
    radius = 130.f;
    sizeType = "H";
  }

  insertParkingQuery->bindValue(":radius", radius);

  if(!isFuel)
  {
    QString type = insertParkingQuery->boundValue(":type", true /* ignore invalid */).toString();
    if(type == "G" || type == "RGA")
      insertParkingQuery->bindValue(":type", type + sizeType);
  }

  // TYPES
  // {"INVALID", QObject::tr("Invalid")},
  // {"UNKNOWN", QObject::tr("Unknown")},
  // {"RGA", QObject::tr("Ramp GA")},
  // {"RGAS", QObject::tr("Ramp GA Small")},
  // {"RGAM", QObject::tr("Ramp GA Medium")},
  // {"RGAL", QObject::tr("Ramp GA Large")},
  // {"RC", QObject::tr("Ramp Cargo")},
  // {"RMC", QObject::tr("Ramp Mil Cargo")},
  // {"RMCB", QObject::tr("Ramp Mil Combat")},
  // {"T", QObject::tr("Tie down")},
  // {"H", QObject::tr("Hangar")},
  // {"G", QObject::tr("Gate")},
  // {"GS", QObject::tr("Gate Small")},
  // {"GM", QObject::tr("Gate Medium")},
  // {"GH", QObject::tr("Gate Heavy")},
  // {"DGA", QObject::tr("Dock GA")},
  // {"FUEL", QObject::tr("Fuel")},
  // {"V", QObject::tr("Vehicles")}
}

void XpAirportWriter::finishStartupLocation()
{
  if(writingStartLocation)
  {
    QString type = insertParkingQuery->boundValue(":type", true /* ignore invalid */).toString();

    if(type.startsWith("G"))
    {
      numParkingGate++;

      if(largestParkingGate.isEmpty() || compareGate(largestParkingGate, type) > 0)
        largestParkingGate = type;
    }

    if(type.startsWith("RGA"))
    {
      numParkingGaRamp++;
      if(largestParkingRamp.isEmpty() || compareRamp(largestParkingRamp, type) > 0)
        largestParkingRamp = type;
    }

    if(type.startsWith("RC"))
      numParkingCargo++;

    if(type.startsWith("RMC"))
    {
      numParkingMilCombat++;
      numParkingMilCargo++;
    }

    insertAirportQuery->bindValue(":largest_parking_ramp", largestParkingRamp);
    insertAirportQuery->bindValue(":largest_parking_gate", largestParkingGate);

    insertParkingQuery->exec();
    insertParkingQuery->clearBoundValues();
    writingStartLocation = false;
  }
}

void XpAirportWriter::writeStartup(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in writeStartup";

  writingStartLocation = true;
  numParking++;
  insertParkingQuery->bindValue(":parking_id", ++curParkingId);
  insertParkingQuery->bindValue(":airport_id", curAirportId);

  Pos pos(at(line, s::LONX).toFloat(), at(line, s::LATY).toFloat());
  airportRect.extend(pos);
  insertParkingQuery->bindValue(":laty", pos.getLatY());
  insertParkingQuery->bindValue(":lonx", pos.getLonX());

  insertParkingQuery->bindValue(":heading", at(line, s::HEADING).toFloat());
  insertParkingQuery->bindValue(":number", -1);
  insertParkingQuery->bindValue(":radius", 50.f);
  insertParkingQuery->bindValue(":airline_codes", QVariant(QVariant::String));
  insertParkingQuery->bindValue(":name", mid(line, s::NAME, true /* ignore error */));
  insertParkingQuery->bindValue(":has_jetway", 0);
  insertParkingQuery->bindValue(":type", "");

  finishStartupLocation();
}

void XpAirportWriter::writeCom(const QStringList& line, AirportRowCode rowCode,
                               const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in writeCom";

  numCom++;
  insertComQuery->bindValue(":com_id", ++curComId);
  insertComQuery->bindValue(":airport_id", curAirportId);

  int frequency = at(line, com::FREQUENCY).toInt() * 10;
  QString name = mid(line, com::NAME, true /* ignore error */);
  insertComQuery->bindValue(":name", name);
  insertComQuery->bindValue(":frequency", frequency);
  insertComQuery->bindValue(":type", "NONE");

  if(rowCode == x::COM_WEATHER)
  {
    // Check name for general weather frequency
    if(name.contains("atis", Qt::CaseInsensitive))
    {
      insertAirportQuery->bindValue(":atis_frequency", frequency);
      insertComQuery->bindValue(":type", "ATIS");
    }
    else if(name.contains("awos", Qt::CaseInsensitive))
    {
      insertAirportQuery->bindValue(":awos_frequency", frequency);
      insertComQuery->bindValue(":type", "AWOS");
    }
    else if(name.contains("asos", Qt::CaseInsensitive))
    {
      insertAirportQuery->bindValue(":asos_frequency", frequency);
      insertComQuery->bindValue(":type", "ASOS");
    }
    else
    {
      insertAirportQuery->bindValue(":atis_frequency", frequency);
      insertComQuery->bindValue(":type", "ATIS");
    }
  }
  else if(rowCode == x::COM_UNICOM)
  {
    insertAirportQuery->bindValue(":unicom_frequency", frequency);
    insertComQuery->bindValue(":type", "UC");
  }
  else if(rowCode == x::COM_TOWER)
  {
    insertAirportQuery->bindValue(":tower_frequency", frequency);
    insertComQuery->bindValue(":type", "T");
  }
  else if(rowCode == x::COM_CLEARANCE)
    insertComQuery->bindValue(":type", "C");
  else if(rowCode == x::COM_GROUND)
    insertComQuery->bindValue(":type", "G");
  else if(rowCode == x::COM_APPROACH)
    insertComQuery->bindValue(":type", "A");
  else if(rowCode == x::COM_DEPARTURE)
    insertComQuery->bindValue(":type", "D");

  insertComQuery->exec();
}

void XpAirportWriter::bindFuel(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindFuel";

  QString type = at(line, 4);
  // Pipe separated list (“|”). Include 1 or more of the following:
  // baggage_loader, baggage_train, crew_car, crew_ferrari, crew_limo, pushback, fuel_liners, fuel_jets, fuel_props, food, gpu

  if(type.contains("fuel_props"))
    insertAirportQuery->bindValue(":has_avgas", 1);

  if(type.contains("fuel_liners") || type.contains("fuel_jets"))
    insertAirportQuery->bindValue(":has_jetfuel", 1);
}

void XpAirportWriter::bindMetadata(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindMetadata";

  QString key = at(line, m::KEY).toLower();
  QString value = mid(line, m::VALUE, true /* ignore error */);

  if(key == "city")
    insertAirportQuery->bindValue(":city", value);
  else if(key == "country")
    insertAirportQuery->bindValue(":country", value);
  else if(key.startsWith("region") && !value.isEmpty()) // Documentation is not clear - region_id or region_code
    insertAirportQuery->bindValue(":region", value);
  else if(key == "datum_lat" && atools::almostNotEqual(value.toFloat(), 0.f))
    airportDatumPos.setLatY(value.toFloat());
  else if(key == "datum_lon" && atools::almostNotEqual(value.toFloat(), 0.f))
    airportDatumPos.setLonX(value.toFloat());

  // 1302 city Seattle
  // 1302 country United States
  // 1302 datum_lat 47.449888889
  // 1302 datum_lon -122.311777778
  // 1302 faa_code SEA
  // 1302 iata_code SEA
  // 1302 icao_code KSEA
}

void XpAirportWriter::writeHelipad(const QStringList& line, const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in writeHelipad";

  Pos pos(at(line, hp::LONX).toFloat(), at(line, hp::LATY).toFloat());

  // Write start position for helipad
  numStart++;
  insertStartQuery->bindValue(":start_id", ++curStartId);
  insertStartQuery->bindValue(":airport_id", curAirportId);
  insertStartQuery->bindValue(":runway_end_id", QVariant(QVariant::Int));
  insertStartQuery->bindValue(":number", ++curHelipadStartNumber);
  insertStartQuery->bindValue(":runway_name", QString("%1").arg(curHelipadStartNumber, 2, 10, QChar('0')));
  insertStartQuery->bindValue(":laty", pos.getLatY());
  insertStartQuery->bindValue(":lonx", pos.getLonX());
  insertStartQuery->bindValue(":type", "H");
  insertStartQuery->bindValue(":altitude", airportAltitude);
  insertStartQuery->bindValue(":heading", at(line, hp::ORIENTATION).toFloat());
  insertStartQuery->exec();

  // Write the helipad
  numHelipad++;
  insertHelipadQuery->bindValue(":helipad_id", ++curHelipadId);
  insertHelipadQuery->bindValue(":airport_id", curAirportId);
  insertHelipadQuery->bindValue(":start_id", curStartId);
  insertHelipadQuery->bindValue(":surface", surfaceToDb(static_cast<Surface>(at(line, rw::SURFACE).toInt()), &context));

  insertHelipadQuery->bindValue(":length", meterToFeet(at(line, hp::LENGTH).toFloat()));
  insertHelipadQuery->bindValue(":width", meterToFeet(at(line, hp::WIDTH).toFloat()));
  insertHelipadQuery->bindValue(":heading", at(line, hp::ORIENTATION).toFloat());

  insertHelipadQuery->bindValue(":type", "H"); // not available
  insertHelipadQuery->bindValue(":is_transparent", 0); // not available
  insertHelipadQuery->bindValue(":is_closed", airportClosed); // From airport name

  insertHelipadQuery->bindValue(":altitude", airportAltitude);

  airportRect.extend(pos);
  insertHelipadQuery->bindValue(":laty", pos.getLatY());
  insertHelipadQuery->bindValue(":lonx", pos.getLonX());

  insertHelipadQuery->exec();
}

void XpAirportWriter::bindRunway(const QStringList& line, AirportRowCode rowCode,
                                 const atools::fs::xp::XpWriterContext& context)
{
  if(ignoringAirport)
    return;

  if(!writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindRunway";

  Pos primaryPos, secondaryPos;
  QString primaryName, secondaryName;
  Surface surface(UNKNOWN);

  // Get runway data for land or water which have different indexes
  if(rowCode == LAND_RUNWAY)
  {
    primaryPos = Pos(at(line, rw::PRIMARY_LONX).toFloat(), at(line, rw::PRIMARY_LATY).toFloat());
    secondaryPos = Pos(at(line, rw::SECONDARY_LONX).toFloat(), at(line, rw::SECONDARY_LATY).toFloat());
    primaryName = at(line, rw::PRIMARY_NUMBER);
    secondaryName = at(line, rw::SECONDARY_NUMBER);
    surface = static_cast<Surface>(at(line, rw::SURFACE).toInt());
  }
  else if(rowCode == WATER_RUNWAY)
  {
    primaryPos = Pos(at(line, rw::WATER_PRIMARY_LONX).toFloat(), at(line, rw::WATER_PRIMARY_LATY).toFloat());
    secondaryPos = Pos(at(line, rw::WATER_SECONDARY_LONX).toFloat(), at(line, rw::WATER_SECONDARY_LATY).toFloat());
    primaryName = at(line, rw::WATER_PRIMARY_NUMBER);
    secondaryName = at(line, rw::WATER_SECONDARY_NUMBER);
    surface = WATER;
  }
  else
    qWarning() << context.messagePrefix() << "Invalid runway code" << rowCode;

  // Calculate end ids
  int primRwEndId = ++curRunwayEndId;
  int secRwEndId = ++curRunwayEndId;

  // Add to index
  airportIndex->addRunwayEnd(airportIcao, primaryName, primRwEndId);
  airportIndex->addRunwayEnd(airportIcao, secondaryName, secRwEndId);

  // Calculate heading and positions
  float lengthMeter = primaryPos.distanceMeterTo(secondaryPos);
  float lengthFeet = meterToFeet(lengthMeter);
  float widthFeet = meterToFeet(at(line, rw::WIDTH).toFloat());
  float primaryHeading = primaryPos.angleDegTo(secondaryPos);
  float secondaryHeading = atools::geo::normalizeCourse(atools::geo::opposedCourseDeg(primaryHeading));
  Pos center = primaryPos.interpolate(secondaryPos, lengthMeter, 0.5f);
  airportRect.extend(primaryPos);
  airportRect.extend(secondaryPos);

  numRunway++;

  // Update airport counts
  if(isSurfaceHard(surface))
    numHardRunway++;

  if(isSurfaceSoft(surface))
    numSoftRunway++;

  if(isSurfaceWater(surface))
    numWaterRunway++;

  // Remember data of longest runway
  QString surfaceStr = surfaceToDb(surface, &context);
  if(lengthFeet > longestRunwayLength)
  {
    longestRunwayLength = lengthFeet;
    longestRunwayWidth = widthFeet;
    longestRunwayHeading = primaryHeading;
    longestRunwaySurface = surfaceStr;
    longestRunwayCenterPos = center;
  }

  insertRunwayQuery->bindValue(":runway_id", primRwEndId);
  insertRunwayQuery->bindValue(":airport_id", curAirportId);
  insertRunwayQuery->bindValue(":primary_end_id", primRwEndId);
  insertRunwayQuery->bindValue(":secondary_end_id", secRwEndId);
  insertRunwayQuery->bindValue(":surface", surfaceStr);

  // Add shoulder surface (X-Plane only)
  int shoulder = at(line, rw::SHOULDER_SURFACE).toInt();
  if(shoulder == 1)
    insertRunwayQuery->bindValue(":shoulder", surfaceToDb(ASPHALT, &context));
  else if(shoulder == 2)
    insertRunwayQuery->bindValue(":shoulder", surfaceToDb(CONCRETE, &context));
  else
    insertRunwayQuery->bindValue(":shoulder", QVariant(QVariant::String));

  insertRunwayQuery->bindValue(":length", lengthFeet);
  insertRunwayQuery->bindValue(":width", widthFeet);
  insertRunwayQuery->bindValue(":heading", primaryHeading);

  if(rowCode == LAND_RUNWAY)
  {
    // Surface markings
    insertRunwayQuery->bindValue(":marking_flags",
                                 markingToDb(static_cast<Marking>(at(line, rw::PRIMARY_MARKINGS).toInt()), &context) |
                                 markingToDb(static_cast<Marking>(at(line, rw::SECONDARY_MARKINGS).toInt()), &context));

    // Lights
    int edgeLights = at(line, rw::EDGE_LIGHTS).toInt();
    if(edgeLights == 0)
      insertRunwayQuery->bindValue(":edge_light", QVariant(QVariant::String));
    else if(edgeLights == 1)
      insertRunwayQuery->bindValue(":edge_light", "L");
    else if(edgeLights == 2)
      insertRunwayQuery->bindValue(":edge_light", "M");
    else if(edgeLights == 3)
      insertRunwayQuery->bindValue(":edge_light", "H");
    else
      qWarning() << context.messagePrefix() << "Invalid edge light value" << edgeLights;

    int centerLights = at(line, rw::CENTER_LIGHTS).toInt();
    if(centerLights == 1)
      insertRunwayQuery->bindValue(":center_light", "M"); // Either none or medium
    else
      insertRunwayQuery->bindValue(":center_light", QVariant(QVariant::String));

    if(edgeLights > 0 || centerLights > 0)
      numLightRunway++;
  }
  else
    insertRunwayQuery->bindValue(":marking_flags", 0);

  insertRunwayQuery->bindValue(":pattern_altitude", 0); // not available
  insertRunwayQuery->bindValue(":has_center_red", 0); // not available
  insertRunwayQuery->bindValue(":primary_lonx", primaryPos.getLonX());
  insertRunwayQuery->bindValue(":primary_laty", primaryPos.getLatY());
  insertRunwayQuery->bindValue(":secondary_lonx", secondaryPos.getLonX());
  insertRunwayQuery->bindValue(":secondary_laty", secondaryPos.getLatY());
  insertRunwayQuery->bindValue(":altitude", airportAltitude);
  insertRunwayQuery->bindValue(":lonx", center.getLonX());
  insertRunwayQuery->bindValue(":laty", center.getLatY());

  // ===========================================================================================
  // Primary end ==============================
  SqlRecord rec = runwayEndRecord;
  rec.setValue(":runway_end_id", primRwEndId);
  rec.setValue(":name", primaryName);
  rec.setValue(":end_type", "P");

  if(rowCode == LAND_RUNWAY)
  {
    rec.setValue(":offset_threshold", meterToFeet(at(line, rw::PRIMARY_DISPLACED_THRESHOLD).toFloat()));
    rec.setValue(":blast_pad", meterToFeet(at(line, rw::PRIMARY_OVERRUN_BLASTPAD).toFloat()));

    QString als = alsToDb(static_cast<ApproachLight>(at(line, rw::PRIMARY_ALS).toInt()), &context);
    if(!als.isEmpty())
    {
      numRunwayEndAls++;
      rec.setValue(":app_light_system_type", als);
    }
    else
      rec.setValue(":app_light_system_type", QVariant(QVariant::String));

    rec.setValue(":has_reils", at(line, rw::PRIMARY_REIL).toInt() > 0);
    rec.setValue(":has_touchdown_lights", at(line, rw::PRIMARY_TDZ_LIGHT).toInt());
  }
  else
  {
    // No lights and markings on water
    rec.setValue(":offset_threshold", 0);
    rec.setValue(":blast_pad", 0);
    rec.setValue(":app_light_system_type", QVariant(QVariant::String));
    rec.setValue(":has_reils", 0);
    rec.setValue(":has_touchdown_lights", 0);
  }

  rec.setValue(":has_end_lights", 0); // not available
  rec.setValue(":num_strobes", 0); // not available
  rec.setValue(":overrun", 0); // not available
  rec.setValue(":has_closed_markings", airportClosed); // From name
  rec.setValue(":has_stol_markings", 0); // not available
  rec.setValue(":is_takeoff", 1); // not available
  rec.setValue(":is_landing", 1); // not available
  rec.setValue(":is_pattern", "N"); // not available

  rec.setValue(":heading", primaryHeading);
  rec.setNull(":altitude");
  rec.setValue(":lonx", primaryPos.getLonX());
  rec.setValue(":laty", primaryPos.getLatY());

  runwayEndRecords.append(rec);

  // ===========================================================================================
  // Secondary end ==============================
  rec = runwayEndRecord;
  rec.setValue(":runway_end_id", secRwEndId);
  rec.setValue(":name", secondaryName);
  rec.setValue(":end_type", "S");

  if(rowCode == LAND_RUNWAY)
  {
    rec.setValue(":offset_threshold", meterToFeet(at(line, rw::SECONDARY_DISPLACED_THRESHOLD).toFloat()));
    rec.setValue(":blast_pad", meterToFeet(at(line, rw::SECONDARY_OVERRUN_BLASTPAD).toFloat()));

    QString als = alsToDb(static_cast<ApproachLight>(at(line, rw::SECONDARY_ALS).toInt()), &context);
    if(!als.isEmpty())
    {
      numRunwayEndAls++;
      rec.setValue(":app_light_system_type", als);
    }
    else
      rec.setValue(":app_light_system_type", QVariant(QVariant::String));

    rec.setValue(":has_reils", at(line, rw::SECONDARY_REIL).toInt() > 0);
    rec.setValue(":has_touchdown_lights", at(line, rw::SECONDARY_TDZ_LIGHT).toInt());
  }
  else
  {
    // No lights and markings on water
    rec.setValue(":offset_threshold", 0);
    rec.setValue(":blast_pad", 0);
    rec.setValue(":app_light_system_type", QVariant(QVariant::String));
    rec.setValue(":has_reils", 0);
    rec.setValue(":has_touchdown_lights", 0);
  }

  rec.setValue(":has_end_lights", 0);
  rec.setValue(":num_strobes", 0);
  rec.setValue(":overrun", 0);
  rec.setValue(":has_closed_markings", airportClosed); // From name
  rec.setValue(":has_stol_markings", 0);
  rec.setValue(":is_takeoff", 1);
  rec.setValue(":is_landing", 1);
  rec.setValue(":is_pattern", "N"); // NONE

  rec.setValue(":heading", secondaryHeading);
  rec.setNull(":altitude");
  rec.setValue(":lonx", secondaryPos.getLonX());
  rec.setValue(":laty", secondaryPos.getLatY());

  runwayEndRecords.append(rec);

  insertRunwayQuery->exec();
  insertRunwayQuery->clearBoundValues();

  // Write start position for primary runway end
  numStart++;
  insertStartQuery->bindValue(":start_id", ++curStartId);
  insertStartQuery->bindValue(":airport_id", curAirportId);
  insertStartQuery->bindValue(":runway_end_id", primRwEndId);
  insertStartQuery->bindValue(":number", QVariant(QVariant::Int));
  insertStartQuery->bindValue(":runway_name", primaryName);
  insertStartQuery->bindValue(":laty", primaryPos.getLatY());
  insertStartQuery->bindValue(":lonx", primaryPos.getLonX());
  insertStartQuery->bindValue(":type", "R");
  insertStartQuery->bindValue(":altitude", airportAltitude);
  insertStartQuery->bindValue(":heading", primaryHeading);
  insertStartQuery->exec();

  // Write start position for secondary runway end
  numStart++;
  insertStartQuery->bindValue(":start_id", ++curStartId);
  insertStartQuery->bindValue(":airport_id", curAirportId);
  insertStartQuery->bindValue(":runway_end_id", secRwEndId);
  insertStartQuery->bindValue(":number", QVariant(QVariant::Int));
  insertStartQuery->bindValue(":runway_name", secondaryName);
  insertStartQuery->bindValue(":laty", secondaryPos.getLatY());
  insertStartQuery->bindValue(":lonx", secondaryPos.getLonX());
  insertStartQuery->bindValue(":type", "R");
  insertStartQuery->bindValue(":altitude", airportAltitude);
  insertStartQuery->bindValue(":heading", secondaryHeading);
  insertStartQuery->exec();
}

void XpAirportWriter::bindAirport(const QStringList& line, AirportRowCode rowCode, const XpWriterContext& context)
{
  if(writingAirport)
    qWarning() << context.messagePrefix() << "Invalid writing airport state in bindAirport";
  if(ignoringAirport)
    qWarning() << context.messagePrefix() << "Invalid ignoring airport state in bindAirport";

  int airportId = ++curAirportId;
  airportIcao = line.value(ap::ICAO);

  writeAirportFile(airportIcao, context.curFileId);

  if(!airportIndex->addAirport(airportIcao, airportId) || !options.isIncludedAirportIdent(airportIcao))
    // Airport was already read before - ignore it completely
    ignoringAirport = true;
  else
  {
    writingAirport = true;

    airportRowCode = rowCode;

    insertAirportQuery->bindValue(":airport_id", airportId);
    insertAirportQuery->bindValue(":file_id", context.curFileId);

    airportAltitude = line.value(ap::ELEVATION).toFloat();

    QString name = mid(line, ap::NAME, true /* ignore error */);
    airportClosed = atools::fs::util::isNameClosed(name);

    if(NAME_INDICATOR.match(name).hasMatch())
      // Remove [H], [S], [g] and [mil] indicators
      name = name.replace(NAME_INDICATOR, "").trimmed();

    // Check military before converting to caps
    bool isMil = atools::fs::util::isNameMilitary(name);
    name = atools::fs::util::capAirportName(name);

    insertAirportQuery->bindValue(":ident", airportIcao);
    insertAirportQuery->bindValue(":name", name);
    insertAirportQuery->bindValue(":fuel_flags", 0); // not available
    insertAirportQuery->bindValue(":has_tower_object", 0);
    insertAirportQuery->bindValue(":is_closed", airportClosed); // extracted from name
    insertAirportQuery->bindValue(":is_military", isMil);
    insertAirportQuery->bindValue(":is_addon", context.flags.testFlag(IS_ADDON));
    insertAirportQuery->bindValue(":num_boundary_fence", 0);

    insertAirportQuery->bindValue(":num_approach", 0); // num_approach filled later when reading CIFP
    insertAirportQuery->bindValue(":num_runway_end_closed", 0); // not available
    // insertAirportQuery->bindValue(":num_runway_end_ils", 0); filled later - nothing to do here
    insertAirportQuery->bindValue(":num_jetway", 0); // not available
    insertAirportQuery->bindValue(":scenery_local_path", context.localPath);
    insertAirportQuery->bindValue(":bgl_filename", context.fileName);
    insertAirportQuery->bindValue(":altitude", airportAltitude);

    insertAirportQuery->bindValue(":has_jetfuel", 0); // filled later
    insertAirportQuery->bindValue(":has_avgas", 0); // filled later
  }
}

void XpAirportWriter::reset()
{
  airportRect = Rect();
  longestRunwayCenterPos = airportPos = airportDatumPos = Pos();

  longestRunwayLength = longestRunwayWidth = longestRunwayHeading = 0;
  longestRunwaySurface = "UNKNOWN";
  numRunway = numSoftRunway = numWaterRunway = numHardRunway = numHelipad = numLightRunway = 0;
  numParkingGate = numParkingGaRamp = numParkingCargo = numParkingMilCargo = numParkingMilCombat = 0;
  numCom = numStart = numRunwayEndVasi = numApron = numTaxiPath = numRunwayEndAls = numParking = 0;
  airportClosed = false;
  airportAltitude = 0.f;
  curHelipadStartNumber = 0;
  airportRowCode = NO_ROWCODE;
  airportIcao.clear();
  runwayEndRecords.clear();
  taxiNodes.clear();
  largestParkingGate.clear();
  largestParkingRamp.clear();
  hasTower = false;

  writingAirport = ignoringAirport = false;
  writingPavementBoundary = writingPavementHoles = writingPavementNewHole = writingStartLocation = false;
}

void XpAirportWriter::finishAirport(const XpWriterContext& context)
{
  if(writingAirport && !ignoringAirport)
  {
    // Update counts
    insertAirportQuery->bindValue(":longest_runway_length", longestRunwayLength);
    insertAirportQuery->bindValue(":longest_runway_width", longestRunwayWidth);
    insertAirportQuery->bindValue(":longest_runway_heading", longestRunwayHeading);
    insertAirportQuery->bindValue(":longest_runway_surface", longestRunwaySurface);
    insertAirportQuery->bindValue(":num_runways", numSoftRunway + numWaterRunway + numHardRunway);
    insertAirportQuery->bindValue(":num_runway_hard", numHardRunway);
    insertAirportQuery->bindValue(":num_runway_soft", numSoftRunway);
    insertAirportQuery->bindValue(":num_runway_water", numWaterRunway);
    insertAirportQuery->bindValue(":num_runway_light", numLightRunway);
    insertAirportQuery->bindValue(":num_helipad", numHelipad);
    insertAirportQuery->bindValue(":num_com", numCom);
    insertAirportQuery->bindValue(":num_runway_end_als", numRunwayEndAls);
    insertAirportQuery->bindValue(":num_starts", numStart);
    insertAirportQuery->bindValue(":num_runway_end_vasi", numRunwayEndVasi);
    insertAirportQuery->bindValue(":num_apron", numApron);
    insertAirportQuery->bindValue(":num_taxi_path", numTaxiPath);

    insertAirportQuery->bindValue(":has_tower_object", hasTower);

    // Rating
    int rating =
      atools::fs::util::calculateAirportRatingXp(context.flags.testFlag(IS_ADDON), context.flags.testFlag(IS_3D),
                                                 hasTower, numTaxiPath, numParking, numApron);
    insertAirportQuery->bindValue(":rating", rating);
    insertAirportQuery->bindValue(":is_3d", context.flags.testFlag(IS_3D));

    insertAirportQuery->bindValue(":num_parking_gate", numParkingGate);
    insertAirportQuery->bindValue(":num_parking_ga_ramp", numParkingGaRamp);
    insertAirportQuery->bindValue(":num_parking_cargo", numParkingCargo);
    insertAirportQuery->bindValue(":num_parking_mil_cargo", numParkingMilCargo);
    insertAirportQuery->bindValue(":num_parking_mil_combat", numParkingMilCombat);

    // Find the bounding rect
    if(!airportRect.isValid())
    {
      qWarning() << context.messagePrefix() << airportIcao << "No bounding rectangle for airport found";
      // Find valid starting point for bounding rectangle
      if(airportDatumPos.isValid())
      {
        airportRect = Rect(airportDatumPos);
        airportPos = airportDatumPos;
      }
      else if(longestRunwayCenterPos.isValid())
      {
        airportRect = Rect(longestRunwayCenterPos);
        airportPos = longestRunwayCenterPos;
      }
      else
        qWarning() << context.messagePrefix() << airportIcao << "Could not determine bounding rectangle for airport";
    }
    else
    {
      if(airportDatumPos.isValid())
      {
        // Check if the datum is nearby the bounding rectangle
        Rect testRect(airportRect);
        testRect.inflate(Pos::POS_EPSILON_100M, Pos::POS_EPSILON_100M);

        if(testRect.contains(airportDatumPos))
          // Optional datum seems to be valid
          airportPos = airportDatumPos;
        else
        {
          // Datum is invalid use runway or center of rect
          // qWarning() << context.messagePrefix() << airportIcao << "Airport datum not within bounding rectangle";
          if(numRunway == 1)
            airportPos = longestRunwayCenterPos;
          else
            airportPos = airportRect.getCenter();
        }
      }
    }

    if(airportRect.isPoint())
      airportRect.inflate(1.f / 60.f, 1.f / 60.f);

    insertAirportQuery->bindValue(":left_lonx", airportRect.getTopLeft().getLonX());
    insertAirportQuery->bindValue(":top_laty", airportRect.getTopLeft().getLatY());
    insertAirportQuery->bindValue(":right_lonx", airportRect.getBottomRight().getLonX());
    insertAirportQuery->bindValue(":bottom_laty", airportRect.getBottomRight().getLatY());

    // Center position
    Pos center = airportPos.isValid() ? airportPos : airportRect.getCenter();
    insertAirportQuery->bindValue(":lonx", center.getLonX());
    insertAirportQuery->bindValue(":laty", center.getLatY());

    insertAirportQuery->bindValue(":mag_var", context.magDecReader->getMagVar(center));

    insertAirportQuery->exec();
    insertAirportQuery->clearBoundValues();

    progress->incNumAirports();

    insertRunwayEndQuery->bindAndExecRecords(runwayEndRecords);
  }

  reset();
}

void XpAirportWriter::writeAirportFile(const QString& icao, int curFileId)
{
  insertAirportFileQuery->bindValue(":airport_file_id", --curAirportFileId);
  insertAirportFileQuery->bindValue(":file_id", curFileId);
  insertAirportFileQuery->bindValue(":ident", icao);
  insertAirportFileQuery->exec();
}

// Compares s1 with s2 and returns an integer less than, equal to, or greater than zero
// if s1 is less than, equal to, or greater than s2.
int XpAirportWriter::compareGate(const QString& gate1, const QString& gate2)
{
  if(gate1 != gate2)
  {
    if(gate1 == "GH")
      return 1;

    if(gate2 == "GH")
      return -1;

    if(gate1 == "GS")
      return -1;

    if(gate2 == "GS")
      return 1;
  }
  return 0;
}

int XpAirportWriter::compareRamp(const QString& ramp1, const QString& ramp2)
{
  if(ramp1 != ramp2)
  {
    if(ramp1 == "RGAL")
      return 1;

    if(ramp2 == "RGAL")
      return -1;

    if(ramp1 == "RGAS")
      return -1;

    if(ramp2 == "RGAS")
      return 1;
  }
  return 0;
}

void XpAirportWriter::initQueries()
{
  deInitQueries();

  SqlUtil util(&db);

  insertAirportQuery = new SqlQuery(db);
  insertAirportQuery->prepare(util.buildInsertStatement("airport"));

  insertRunwayQuery = new SqlQuery(db);
  insertRunwayQuery->prepare(util.buildInsertStatement("runway"));

  insertRunwayEndQuery = new SqlQuery(db);
  insertRunwayEndQuery->prepare(util.buildInsertStatement("runway_end"));

  insertHelipadQuery = new SqlQuery(db);
  insertHelipadQuery->prepare(util.buildInsertStatement("helipad"));

  insertComQuery = new SqlQuery(db);
  insertComQuery->prepare(util.buildInsertStatement("com"));

  insertStartQuery = new SqlQuery(db);
  insertStartQuery->prepare(util.buildInsertStatement("start"));

  insertParkingQuery = new SqlQuery(db);
  insertParkingQuery->prepare(util.buildInsertStatement("parking", QString(), {"pushback"}));

  insertApronQuery = new SqlQuery(db);
  insertApronQuery->prepare(util.buildInsertStatement("apron", QString(), {"vertices", "vertices2", "triangles"}));

  insertTaxiQuery = new SqlQuery(db);
  insertTaxiQuery->prepare(util.buildInsertStatement("taxi_path", QString(), {"start_dir", "end_dir"}));

  insertAirportFileQuery = new SqlQuery(db);
  insertAirportFileQuery->prepare(util.buildInsertStatement("airport_file"));
}

void XpAirportWriter::deInitQueries()
{
  delete insertAirportQuery;
  insertAirportQuery = nullptr;

  delete insertRunwayQuery;
  insertRunwayQuery = nullptr;

  delete insertRunwayEndQuery;
  insertRunwayEndQuery = nullptr;

  delete insertHelipadQuery;
  insertHelipadQuery = nullptr;

  delete insertComQuery;
  insertComQuery = nullptr;

  delete insertStartQuery;
  insertStartQuery = nullptr;

  delete insertParkingQuery;
  insertParkingQuery = nullptr;

  delete insertApronQuery;
  insertApronQuery = nullptr;

  delete insertTaxiQuery;
  insertTaxiQuery = nullptr;

  delete insertAirportFileQuery;
  insertAirportFileQuery = nullptr;
}

} // namespace xp
} // namespace fs
} // namespace atools
