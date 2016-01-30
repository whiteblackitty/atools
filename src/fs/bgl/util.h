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

#ifndef BGL_UTIL_H_
#define BGL_UTIL_H_

#include <QString>

namespace atools {
namespace fs {
namespace bgl {
namespace util {

template<typename TYPE>
QString enumToStr(QString func(TYPE t), TYPE type)
{
  QString retval = func(type);
  if(retval == "NONE" || retval == "NO")
    return "";
  else
    return retval;
}

inline int meterToNm(float meters)
{
  return static_cast<int>(round(meters / 1852.0f));
}

inline int meterToFeet(float meters, int precision = 0)
{
  if(precision == 0)
    return static_cast<int>(round(meters * 3.281f));

  {
    int factor = static_cast<int>(pow(10., precision));
    return static_cast<int>(round(meters * 3.281f / factor)) * factor;
  }
}

template<typename TYPE>
bool isFlagSet(TYPE bitfield, TYPE flag)
{
  return (bitfield & flag) == flag;
}

template<typename TYPE>
bool isFlagNotSet(TYPE bitfield, TYPE flag)
{
  return (bitfield & flag) == 0;
}

} // namespace util
} // namespace bgl
} // namespace fs
} // namespace atools

#endif /* BGL_UTIL_H_ */