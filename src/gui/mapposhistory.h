/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
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

#ifndef ATOOLS_GUI_MAPPOSHISTORY_H
#define ATOOLS_GUI_MAPPOSHISTORY_H

#include "geo/pos.h"

#include <QObject>
#include <QApplication>

namespace atools {
namespace gui {

class MapPosHistoryEntry
{
public:
  MapPosHistoryEntry();
  MapPosHistoryEntry(const MapPosHistoryEntry& other);
  MapPosHistoryEntry(atools::geo::Pos position, double mapDistance, qint64 mapTimestamp = 0L);
  ~MapPosHistoryEntry();

  /* Does not compare the timestamp */
  bool operator==(const MapPosHistoryEntry& other) const;
  bool operator!=(const MapPosHistoryEntry& other) const;

  double getDistance() const
  {
    return distance;
  }

  qint64 getTimestamp() const
  {
    return timestamp;
  }

  const atools::geo::Pos& getPos() const
  {
    return pos;
  }

  bool isValid() const
  {
    return pos.isValid();
  }

private:
  friend QDebug operator<<(QDebug debug, const MapPosHistoryEntry& entry);

  friend QDataStream& operator<<(QDataStream& out, const MapPosHistoryEntry& obj);

  friend QDataStream& operator>>(QDataStream& in, MapPosHistoryEntry& obj);

  qint64 timestamp = 0L;
  atools::geo::Pos pos;
  double distance = 0.;
};

const MapPosHistoryEntry EMPTY_MAP_POS;

class MapPosHistory :
  public QObject
{
  Q_OBJECT

public:
  explicit MapPosHistory(QObject *parent = 0);
  virtual ~MapPosHistory();

  const MapPosHistoryEntry& next();
  const MapPosHistoryEntry& back();
  const MapPosHistoryEntry& current() const;

  void addEntry(atools::geo::Pos pos, double distance);

  void saveState(const QString& keyPrefix);
  void restoreState(const QString& keyPrefix);

private:
  const int MAX_MS_FOR_NEW_ENTRY = 200;
  const int MAX_NUMBER_OF_ENTRIES = 50;

  QList<MapPosHistoryEntry> entries;
  int currentIndex = -1;

signals:
  void historyChanged(int minIndex, int curIndex, int maxIndex);

};

} // namespace gui
} // namespace atools

Q_DECLARE_METATYPE(atools::gui::MapPosHistoryEntry);
Q_DECLARE_TYPEINFO(atools::gui::MapPosHistoryEntry, Q_PRIMITIVE_TYPE);

#endif // ATOOLS_GUI_MAPPOSHISTORY_H