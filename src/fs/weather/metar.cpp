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

#include "fs/weather/metar.h"

#include "fs/weather/metarparser.h"
#include "atools.h"

#include <QStringList>
#include <QDebug>
#include <QRegularExpression>

namespace atools {
namespace fs {
namespace weather {

const static QRegularExpression WIND("^(\\d{3}|VRB)\\d{1,3}(G\\d{2,3})?(KT|KMH|MPS)$");
const static QRegularExpression VARIABLE_WIND("^\\d{3}V\\d{3}$");
const static QRegularExpression LONG_VISIBILITY("^(\\d{3})(SM|KM)$");
const static QRegularExpression VISIBILITY_KM("^(\\d{1,2})KM$");
const static QRegularExpression TEMPERATURE("^([-M]?)(\\d{1,2})/([-M]?)(\\d{1,2})$");
const static QRegularExpression CLOUD("^([0-8])(CI|CS|CC|AS|AC|SC|NS|ST|CU|CB)([0-9]{3})$");
const static QRegularExpression NOAA_DATE("^\\d{1,4}/\\d{1,2}/\\d{1,2}$"); // 2007/10/01 03:47
const static QRegularExpression NOAA_TIME("^\\d{1,2}:\\d{1,2}$"); // 2007/10/01 03:47

// CI Cirrus
// CS Cirro-stratus (maps to CI)
// CC Cirro-cumulus (maps to CI)
// AS Alto-stratus (maps to ST)
// AC Alto-cumulus (maps to CU)
// SC Strato-cumulus (maps to CU)
// NS Nimbo-stratus (maps to ST)
// ST Stratus
// CU Cumulus
// CB Cumulo-nimbus
// Note that not all of these cloud types are supported, so a number are
// mapped to those which are. This does mean that a write followed by a read of
// Metar data might not give identical strings.
// ESP extension:
// &TT000FTPQBBBI
// where:
// TT - Cloud type, one of: the list above (for example, CI or CB).
// If this entry is different from the NNN entry above, this entry will take priority.
// 000 - Unused.
// F - Top of cloud, one of: F (flat), R (round), A (anvil)
// T - Turbulence, one of: N - None (default), O - Light, L - Light, M - Moderate, H - Heavy, S - Severe
// P - Precipitation, one of: V (very light), L (light), M (moderate), H (heavy) D (dense)
// Q - Type of precipitation, one of: N (none), R (rain), F (freezing rain), H (hail), S (snow)
// BBB - Coded base height, the precipitation ends at this height, set to 0 for it to land on the ground
// I - icing rate, one of: N (none), T (trace), L (light), M (moderate), S (severe)

const static QRegularExpression CLOUD_EXTENSION(
  "(CI|CS|CC|AS|AC|SC|NS|ST|CU|CB)...[FRA][NOLMHS]([VLMHD])([NRFHS])(\\d\\d\\d)[NTLMS]");

const static QVector<QString> CLOUD_DENSITIES({"CLR", "FEW", "FEW", "SCT",
                                               "SCT", "BKN", "BKN", "BKN", "OVC"});

Metar::Metar(const QString& metarString, const QString& metarStation, const QDateTime& metarTimestamp,
             bool isSimFormat)
  : metar(metarString), station(metarStation), simFormat(isSimFormat), timestamp(metarTimestamp)
{
  buildCleanMetar();

  try
  {
    parsed = new MetarParser(cleanMetar);
  }
  catch(const std::exception& e)
  {
    qWarning() << "Exception while parsing metar" << cleanMetar << ":" << e.what();
    delete parsed;
    parsed = new MetarParser(QString());
  }
}

Metar::~Metar()
{
  delete parsed;
}

// K53S&A1 000000Z 24705G06KT&D975NG 13520KT&A1528NG 129V141 9999 2ST025&ST001FNVN002N
// 1CI312&CI001FNVN002N 13/12 07/05&A1528 Q1009 @@@ 50 7 135 20 |
void Metar::buildCleanMetar()
{
  int numWind = 0, numVar = 0, numTmp = 0;
  if(simFormat)
  {
    // FSX gives the precipidation indication only in the cloud extension
    QStringList met = metar.section("@@@", 0, 0).simplified().split(" ");
    QString precipitation;
    for(const QString& str : met)
    {
      QString extension = str.section("&", 1, 1).simplified();

      QRegularExpressionMatch extensionCloudMatch = CLOUD_EXTENSION.match(extension);
      if(extensionCloudMatch.hasMatch())
      {
        // V (very light), L (light), M (moderate), H (heavy) D (dense)
        QString intensity = extensionCloudMatch.captured(2);
        // Type of precipitation, one of: N (none), R (rain), F (freezing rain), H (hail), S (snow)
        QString type = extensionCloudMatch.captured(3);
        int altitude = extensionCloudMatch.captured(4).toInt();
        if(altitude == 0)
        {
          // If altitude is 0 precipitation extends to the ground
          if(intensity == "V" || intensity == "L")
            precipitation += "-";
          // else if(intensity == "M")
          else if(intensity == "H" || intensity == "D")
            precipitation += "+";

          if(type == "N")
            precipitation.clear();
          else if(type == "R")
            precipitation += "RA";
          else if(type == "F")
            precipitation += "RAFZ";
          else if(type == "H")
            precipitation += "FR";
          else if(type == "S")
            precipitation += "SN";
          break;
        }
      }
    }

    bool precipitationAdded = false;
    QStringList retval;
    for(const QString& str : met)
    {
      QString cleanStr = str.section("&", 0, 0).simplified();

      if(cleanStr == "000000Z" && timestamp.isValid())
        // Fix empty timestamp
        cleanStr = timestamp.toString("ddHHmm") + "Z";
      else if(cleanStr == "????")
        // Replace pattern from interpolated with real station name
        cleanStr = station;
      else
      {
        bool isWind = WIND.match(cleanStr).hasMatch();
        bool isVar = VARIABLE_WIND.match(cleanStr).hasMatch();

        if(isWind)
          numWind++;
        if(isVar)
          numVar++;

        if(isWind && numWind > 1)
          // Skip all duplicate wind indications
          continue;
        if(isVar && numVar > 1)
          // Skip all duplicate visibility indications
          continue;

        QRegularExpressionMatch visMatch = LONG_VISIBILITY.match(cleanStr);
        if(visMatch.hasMatch())
          // Fix too long three digit visibility
          cleanStr = "99" + visMatch.captured(2);

        QRegularExpressionMatch visMatchSm = VISIBILITY_KM.match(cleanStr);
        if(visMatchSm.hasMatch())
          // Convert visibility from km to nm
          cleanStr =
            QString::number(atools::roundToInt(
                              atools::geo::meterToNm(visMatchSm.captured(1).toInt() * 1000))) + "SM";

        QRegularExpressionMatch tmpMatch = TEMPERATURE.match(cleanStr);
        if(tmpMatch.hasMatch())
        {
          // Fix temperature - 1-2 digits to 2 digits and "-" to "M"
          numTmp++;

          if(numTmp > 1)
            // Skip duplicate temperature indications
            continue;

          cleanStr = QString("%1%2/%3%4").
                     arg(tmpMatch.captured(1).replace("-", "M")).
                     arg(tmpMatch.captured(2).toInt(), 2, 10, QChar('0')).
                     arg(tmpMatch.captured(3).replace("-", "M")).
                     arg(tmpMatch.captured(4).toInt(), 2, 10, QChar('0'));
        }

        QRegularExpressionMatch cldMatch = CLOUD.match(cleanStr);
        if(cldMatch.hasMatch())
        {
          if(!precipitationAdded)
          {
            // Add the precipitation as extracted from the cloud extension
            retval.append(precipitation);
            precipitationAdded = true;
          }

          // Cloud density from oktas to text
          cleanStr = CLOUD_DENSITIES.at(cldMatch.captured(1).toInt());

          // Skip cloud type and add altitude
          cleanStr += cldMatch.captured(3);
        }
      }
      retval.append(cleanStr);
    }

    cleanMetar = retval.join(" ").simplified().toUpper();
  }
  else
    cleanMetar = metar;
}

void Metar::test()
{
  static QStringList metars(
    {
      "2007/10/01 03:47 KTDO 010347Z 14005KT 7SM OVC12 10/06 A2984 RMK SLP105 LAST",
      "2016/08/08 03:00 MGTK 080300Z 00000KT CAVOK 23/23 Q1014 A2994 SCT090",
      "CYXE 261800Z CCA 25013KT 15SM SCT030 BKN090 05/03 A2988 RMK SC4AC2 SLP144",

      "CWSP&A21 000000Z 24705G06KT&D975NG 13520KT&A1508NG 129V141 48KM&B58&D3500 6ST024&ST001FNLR001N 2CU041&CU001FNVR001N 3CI311&CI001FNVN001N 13/12 07/05&A1508 Q1009 @@@ 50 7 135 20 |",
      "CWEB&A6 000000Z 33514G23KT&D980MM 330V340 80KM&B-378&D3048 CLR 20/15 Q0989",
      "CWEL&A4 000000Z 24705G06KT&D975NG 13520KT&A1524NG 129V141 16KM&B74&D3500 8ST025&ST001FNMR002N 4CU041&CU001FNVR002N 5CI312&CI001FNVN002N 13/12 07/05&A1524 Q1009 @@@ 50 7 135 20 |",
      "CWEZ&A24 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1416LG 058V068 05920G25KT&A2416LG 046V071 32KM&B-33&D2000 7ST046&ST001FNHS000L -2/-4 -9/-11&A1416 -17/-19&A2416 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "CWEZ&A24 000000Z 24705G06KT&D975NG 13520KT&A1505NG 129V141 48KM&B55&D3500 4ST024&ST001FNVR001N 1CI311&CI001FNVN001N 13/12 07/05&A1505 Q1009 @@@ 50 7 135 20 |",
      "CWEZ&A24 000000Z 30110KT&D0NG 300V302 30125KT&A1031LG 295V307 80KM&B-418&D3048 3CU033&CU001FLLN000N 6CI329&CI001FNMN000L 20/12 15/12&A1031 Q1010 @@@ 35 15 301 25 |",
      "CWGT&A4 000000Z 30110KT&D0NG 300V302 30125KT&A1050LG 295V307 80KM&B-399&D3048 1CU034&CU001FLLN000N 13/12 05/04&A1050 Q0996 @@@ 35 5 301 25 |",
      "CWMM&A4 000000Z 30110KT&D0NG 300V302 30125KT&A1050LG 295V307 80KM&B-399&D3048 3CU034&CU001FLLN000N 6CI329&CI001FNMN000L 20/12 15/12&A1050 Q1010 @@@ 35 15 301 25 |",
      "CWMM&A4 000000Z 33514G23KT&D980MM 330V340 80KM&B-376&D3048 CLR 20/15 Q0989",
      "CWQC&A2 000000Z 30110KT&D0NG 300V302 30125KT&A1053LG 295V307 80KM&B-396&D3048 4ST016&ST000FNVN000N 13/12 05/04&A1053 Q0996 @@@ 35 5 301 25 |",
      "CWQC&A2 000000Z 32514G23KT&D980MM 320V330 31522G30KT&A1076MM 311V318 80KM&B-373&D3048 8CB018&CB001FSDR000N 20/15 Q0989 @@@ 35 20 315 22 |",
      "CWWK&A14 000000Z 24705G06KT&D975NG 13520KT&A1514NG 129V141 48KM&B64&D3500 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 13/12 07/05&A1514 Q1009 @@@ 50 7 135 20 |",
      "CWWK&A14 000000Z 33514G23KT&D980MM 330V340 80KM&B-386&D3048 CLR 20/15 Q0989",
      "CYPW&A130 000000Z 24705G06KT&D975NG 13520KT&A1399NG 129V141 32KM&B-50&D3500 4ST021&ST001FNVR000N 6CI308&CI001FNVN000N 13/12 07/05&A1399 Q1009 @@@ 50 7 135 20 |",
      "CYPW&A130 000000Z 34418G28KT&D980HM 333V355 33530G36KT&A948HS 320V349 80KM&B-501&D3048 8CB014&CB001FSDR000N 20/18 Q0989 @@@ 35 20 335 30 |",
      "CYYJ&A19 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1421LG 058V068 05920G25KT&A2421LG 046V071 32KM&B-28&D2000 7ST046&ST001FNHS000L -2/-4 -9/-11&A1421 -17/-19&A2421 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "CYYJ&A19 000000Z 30110KT&D0NG 300V302 30125KT&A1036LG 295V307 64KM&B-413&D3048 3CU034&CU001FLLN000N 8CI329&CI001FNMN000L 20/12 15/12&A1036 Q1010 @@@ 35 15 301 25 |",
      "K43S&A3 000000Z 24705G06KT&D975NG 13520KT&A1525NG 129V141 9999 CLR 13/12 07/05&A1525 Q1009 @@@ 50 7 135 20 |",
      "K53S&A1 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1439LG 058V068 05920G25KT&A2439LG 046V071 32KM&B-10&D2000 5ST047&ST001FNHS000L -2/-4 -9/-11&A1439 -17/-19&A2439 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "K53S&A1 000000Z 24705G06KT&D975NG 13520KT&A1528NG 129V141 9999 2ST025&ST001FNVN002N 1CI312&CI001FNVN002N 13/12 07/05&A1528 Q1009 @@@ 50 7 135 20 |",
      "K53S&A1 000000Z 30110KT&D0NG 300V302 30125KT&A1054LG 295V307 80KM&B-395&D3048 1CU025&CU000FLVN000N 6CI329&CI001FNMN000L 20/12 15/12&A1054 Q1010 @@@ 35 15 301 25 |",
      "K76S&A57 000000Z 30110KT&D0NG 300V302 30125KT&A997LG 295V307 80KM&B-452&D3048 6CI327&CI001FNMN000L 20/12 15/12&A997 Q1010 @@@ 35 15 301 25 |",
      "K76S&A57 000000Z 33514G23KT&D980MM 330V340 80KM&B-429&D3048 CLR 20/15 Q0989",
      "K84S&A5 000000Z 30110KT&D0NG 300V302 30125KT&A1049LG 295V307 80KM&B-400&D3048 6CI329&CI001FNMN000L 11/08 15/12&A1049 Q0989 @@@ 35 15 301 25 |",
      "K96S&A12 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1428LG 058V068 05920G25KT&A2428LG 046V071 32KM&B-21&D2000 5ST046&ST001FNHS000L -2/-4 -9/-11&A1428 -17/-19&A2428 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "K97S&A1 000000Z 24705G06KT&D975NG 13520KT&A1528NG 129V141 9999 CLR 13/12 07/05&A1528 Q1009 @@@ 50 7 135 20 |",
      "K97S&A1 000000Z 30110KT&D0NG 300V302 30125KT&A1054LG 295V307 80KM&B-395&D3048 1ST016&ST000FLVN000N 11/08 05/04&A1054 Q0989 @@@ 35 5 301 25 |",
      "KBLI&A51 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1389LG 058V068 05920G25KT&A2389LG 046V071 32KM&B-60&D2000 5ST045&ST001FNHS000L -2/-4 -9/-11&A1389 -17/-19&A2389 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KFHR&A34 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1406LG 058V068 05920G25KT&A2406LG 046V071 32KM&B-43&D2000 7ST046&ST001FNHS000L -2/-4 -9/-11&A1406 -17/-19&A2406 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KFHR&A34 000000Z 24705G06KT&D975NG 13520KT&A1495NG 129V141 48KM&B45&D3500 4ST024&ST001FNVR001N 1CI311&CI001FNVN001N 13/12 07/05&A1495 Q1009 @@@ 50 7 135 20 |",
      "KFHR&A34 000000Z 30110KT&D0NG 300V302 30125KT&A1021LG 295V307 80KM&B-428&D3048 1CU033&CU001FLLN000N 6CI328&CI001FNMN000L 20/12 15/12&A1021 Q1010 @@@ 35 15 301 25 |",
      "KFHR&A34 000000Z 33514G23KT&D980MM 330V340 80KM&B-405&D3048 CLR 20/15 Q0989",
      "KGRF&A91 000000Z 33514G23KT&D980MM 330V340 80KM&B-463&D3048 CLR 20/15 Q0989",
      "KHQM&A3 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1436LG 058V068 05920G25KT&A2436LG 046V071 32KM&B-13&D2000 7ST047&ST001FNHS000L -2/-4 -9/-11&A1436 -17/-19&A2436 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KHQM&A3 000000Z 33514G23KT&D980MM 330V340 80KM&B-375&D3048 CLR 20/15 Q0989",
      "KNOW&A3 000000Z 24705G06KT&D975NG 13520KT&A1525NG 129V141 9999 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 13/12 07/05&A1525 Q1009 @@@ 50 7 135 20 |",
      "KNUW&A13 000000Z 30110KT&D0NG 300V302 30125KT&A1041LG 295V307 80KM&B-408&D3048 6CI329&CI001FNMN000L 20/12 15/12&A1041 Q1010 @@@ 35 15 301 25 |",
      "KNUW&A13 000000Z 33514G23KT&D980MM 330V340 80KM&B-385&D3048 CLR 20/15 Q0989",
      "KPAE&A185 000000Z 24705G06KT&D975NG 13520KT&A1344NG 129V141 9999 CLR 13/12 07/05&A1344 Q1009 @@@ 50 7 135 20 |",
      "KPAE&A185 000000Z 30110KT&D0NG 300V302 30125KT&A870LG 295V307 80KM&B-579&D3048 2ST010&ST000FLVN000N 1CU028&CU001FLLN000N 11/08 05/04&A870 Q0989 @@@ 35 5 301 25 |",
      "KPWT&A134 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1306LG 058V068 05920G25KT&A2306LG 046V071 32KM&B-143&D2000 7ST042&ST001FNHS000L -2/-4 -9/-11&A1306 -17/-19&A2306 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KS38&A17 000000Z 24705G06KT&D975NG 13520KT&A1511NG 129V141 9999 CLR 13/12 07/05&A1511 Q1009 @@@ 50 7 135 20 |",
      "KS88&A42 000000Z 30110KT&D0NG 300V302 30125KT&A1013LG 295V307 80KM&B-436&D3048 CLR 11/08 05/04&A1013 Q0989 @@@ 35 5 301 25 |",
      "KTCM&A98 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1342LG 058V068 05920G25KT&A2342LG 046V071 32KM&B-107&D2000 7ST044&ST001FNHS000L -2/-4 -9/-11&A1342 -17/-19&A2342 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KTCM&A98 000000Z 33514G23KT&D980MM 330V340 80KM&B-469&D3048 CLR 20/15 Q0989",
      "KZSE&A99 000000Z 24705G06KT&D975NG 13520KT&A1430NG 129V141 9999 CLR 13/12 07/05&A1430 Q1009 @@@ 50 7 135 20 |",
      "CYAZ&A24 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1416LG 058V068 05920G25KT&A2416LG 046V071 32KM&B-33&D2000 7ST046&ST001FNHS000L -2/-4 -9/-11&A1416 -17/-19&A2416 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "CYAZ&A24 000000Z 30110KT&D0NG 300V302 30125KT&A1031LG 295V307 80KM&B-418&D3048 1CU033&CU001FLLN000N 11/08 05/04&A1031 Q0989 @@@ 35 5 301 25 |",
      "CYAZ&A24 000000Z 33514G23KT&D980MM 330V340 80KM&B-395&D3048 6CU018&CU001FHHR000N 20/15 Q0989",
      "CYBL&A105 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1335LG 058V068 05920G25KT&A2335LG 046V071 32KM&B-114&D2000 7ST043&ST001FNHS000L -2/-4 -9/-11&A1335 -17/-19&A2335 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "CYBL&A105 000000Z 24705G06KT&D975NG 13520KT&A1424NG 129V141 48KM&B-25&D3500 2ST022&ST001FNVN000N 13/12 07/05&A1424 Q1009 @@@ 50 7 135 20 |",
      "CYBL&A105 000000Z 33514G23KT&D980MM 330V340 33530G36KT&A973HS 320V349 80KM&B-476&D3048 8CU015&CU001FHHR000N 20/15 Q0989 @@@ 35 20 335 30 |",
      "CYCD&A27 000000Z 30110KT&D0NG 300V302 30125KT&A1027LG 295V307 64KM&B-422&D3048 4CU033&CU001FLLR000N 18/12 05/04&A1027 Q0999 @@@ 35 5 301 25 |",
      "CYPW&A130 000000Z 30110KT&D0NG 300V302 30125KT&A925LG 295V307 80KM&B-524&D3048 2ST015&ST000FLVN000N 11/08 05/04&A925 Q0989 @@@ 35 5 301 25 |",
      "CYPW&A130 000000Z 34418G28KT&D980HM 333V355 33530G36KT&A948HS 320V349 80KM&B-501&D3048 8CB014&CB001FSDR000N 20/18 Q0989 @@@ 35 20 335 30 |",
      "CYQQ&A25 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1414LG 058V068 05920G25KT&A2414LG 046V071 32KM&B-35&D2000 7ST046&ST001FNHS000L -2/-4 -9/-11&A1414 -17/-19&A2414 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "CYQQ&A25 000000Z 30110KT&D0NG 300V302 30125KT&A1029LG 295V307 80KM&B-420&D3048 CLR 11/08 05/04&A1029 Q0989 @@@ 35 5 301 25 |",
      "CYQQ&A25 000000Z 34418G28KT&D980HM 333V355 33530G36KT&A1052HS 320V349 80KM&B-397&D3048 8CB018&CB001FSDR000N 20/18 Q0989 @@@ 35 20 335 30 |",
      "CYVR&A3 000000Z 24705G06KT&D975NG 13520KT&A1525NG 129V141 48KM&B75&D3500 6ST025&ST001FNLR002N 2CU041&CU001FNVR002N 3CI312&CI001FNVN002N 13/12 07/05&A1525 Q1009 @@@ 50 7 135 20 |",
      "CYVR&A3 000000Z 33514G23KT&D980MM 330V340 80KM&B-375&D3048 2CI330&CI001FMVN000N 20/15 Q0989",
      "CYWH&A4 000000Z 33514G23KT&D980MM 330V340 80KM&B-376&D3048 CLR 20/15 Q0989",
      "CYXX&A57 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1382LG 058V068 05920G25KT&A2382LG 046V071 32KM&B-67&D2000 7ST045&ST001FNHS000L -2/-4 -9/-11&A1382 -17/-19&A2382 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "CYXX&A57 000000Z 33514G23KT&D980MM 330V340 80KM&B-429&D3048 CLR 20/15 Q0989",
      "CYYJ&A19 000000Z 30110KT&D0NG 300V302 30125KT&A1036LG 295V307 64KM&B-413&D3048 3CU034&CU001FLLN000N 8CI329&CI001FNMN000L 20/12 15/12&A1036 Q1010 @@@ 35 15 301 25 |",
      "KAST&A4 000000Z 30110KT&D0NG 300V302 30125KT&A1050LG 295V307 80KM&B-399&D3048 4ST022&ST000FLVN000N 1CU034&CU001FLLN000N 18/12 05/04&A1050 Q0999 @@@ 35 5 301 25 |",
      "KAWO&A42 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1398LG 058V068 05920G25KT&A2398LG 046V071 32KM&B-51&D2000 7ST045&ST001FNHS000L -2/-4 -9/-11&A1398 -17/-19&A2398 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KAWO&A42 000000Z 33514G23KT&D980MM 330V340 80KM&B-413&D3048 CLR 20/15 Q0989",
      "KBFI&A4 000000Z 30110KT&D0NG 300V302 30125KT&A1050LG 295V307 80KM&B-399&D3048 1CU034&CU001FLLN000N 13/12 05/04&A1050 Q0996 @@@ 35 5 301 25 |",
      "KBLI&A51 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1389LG 058V068 05920G25KT&A2389LG 046V071 32KM&B-60&D2000 5ST045&ST001FNHS000L -2/-4 -9/-11&A1389 -17/-19&A2389 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KBLI&A51 000000Z 33514G23KT&D980MM 330V340 80KM&B-422&D3048 CLR 20/15 Q0989",
      "KCLM&A89 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1351LG 058V068 05920G25KT&A2351LG 046V071 32KM&B-98&D2000 7ST044&ST001FNHS000L -2/-4 -9/-11&A1351 -17/-19&A2351 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KHQM&A3 000000Z 30110KT&D0NG 300V302 30125KT&A1051LG 295V307 80KM&B-398&D3048 3ST016&ST000FNVN000N 6CI329&CI001FNMN000L 11/08 15/12&A1051 Q0989 @@@ 35 15 301 25 |",
      "KHQM&A3 000000Z 33514G23KT&D980MM 330V340 80KM&B-375&D3048 CLR 20/15 Q0989",
      "KNOW&A3 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1436LG 058V068 05920G25KT&A2436LG 046V071 32KM&B-13&D2000 7ST047&ST001FNHS000L -2/-4 -9/-11&A1436 -17/-19&A2436 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KNOW&A3 000000Z 24705G06KT&D975NG 13520KT&A1525NG 129V141 9999 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 13/12 07/05&A1525 Q1009 @@@ 50 7 135 20 |",
      "KNOW&A3 000000Z 33514G23KT&D980MM 330V340 80KM&B-375&D3048 CLR 20/15 Q0989",
      "KNRA&A60 000000Z 30110KT&D0NG 300V302 30125KT&A994LG 295V307 80KM&B-455&D3048 6CI327&CI001FNMN000L 20/12 15/12&A994 Q1010 @@@ 35 15 301 25 |",
      "KNUW&A13 000000Z 24705G06KT&D975NG 13520KT&A1515NG 129V141 9999 2ST025&ST001FNVN002N 1CI312&CI001FNVN002N 13/12 07/05&A1515 Q1009 @@@ 50 7 135 20 |",
      "KOLM&A63 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1377LG 058V068 05920G25KT&A2377LG 046V071 32KM&B-72&D2000 5ST045&ST001FNHS000L -2/-4 -9/-11&A1377 -17/-19&A2377 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KOLM&A63 000000Z 33514G23KT&D980MM 330V340 80KM&B-434&D3048 CLR 20/15 Q0989",
      "KPAE&A185 000000Z 30110KT&D0NG 300V302 30125KT&A870LG 295V307 80KM&B-579&D3048 2ST010&ST000FLVN000N 1CU028&CU001FLLN000N 11/08 05/04&A870 Q0989 @@@ 35 5 301 25 |",
      "KPWT&A134 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1306LG 058V068 05920G25KT&A2306LG 046V071 32KM&B-143&D2000 7ST042&ST001FNHS000L -2/-4 -9/-11&A1306 -17/-19&A2306 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KSEA&A131 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1309LG 058V068 05920G25KT&A2309LG 046V071 32KM&B-140&D2000 7ST042&ST001FNHS000L -2/-4 -9/-11&A1309 -17/-19&A2309 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KSEA&A131 000000Z 30110KT&D0NG 300V302 30125KT&A924LG 295V307 80KM&B-525&D3048 1CU030&CU001FLLN000N 13/12 05/04&A924 Q0996 @@@ 35 5 301 25 |",
      "KSEA&A131 000000Z 33514G23KT&D980MM 330V340 80KM&B-502&D3048 CLR 20/15 Q0989",
      "KSHN&A83 000000Z 30110KT&D0NG 300V302 30125KT&A972LG 295V307 80KM&B-477&D3048 1ST015&ST000FLVN000N 11/08 05/04&A972 Q0989 @@@ 35 5 301 25 |",
      "KSHN&A83 000000Z 33514G23KT&D1000MM 330V340 80KM&B-454&D3048 CLR 20/15 Q0989",
      "KTCM&A98 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1342LG 058V068 05920G25KT&A2342LG 046V071 32KM&B-107&D2000 7ST044&ST001FNHS000L -2/-4 -9/-11&A1342 -17/-19&A2342 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KTCM&A98 000000Z 33514G23KT&D980MM 330V340 80KM&B-469&D3048 CLR 20/15 Q0989",
      "KTDO&A113 000000Z 30110KT&D0NG 300V302 30125KT&A941LG 295V307 80KM&B-508&D3048 1ST013&ST000FLVN000N 4CU030&CU001FLLR000N 18/12 05/04&A941 Q0999 @@@ 35 5 301 25 |",
      "KTIW&A89 000000Z 04612G15KT&D985NG 036V056 06315G18KT&A1351LG 058V068 05920G25KT&A2351LG 046V071 32KM&B-98&D2000 7ST044&ST001FNHS000L -2/-4 -9/-11&A1351 -17/-19&A2351 Q1001 @@@ 47 -9 63 15 | 80 -17 59 20 |",
      "KTIW&A89 000000Z 33514G23KT&D980MM 330V340 80KM&B-460&D3048 CLR 20/15 Q0989",
      "KUIL&A58 000000Z 33514G23KT&D980MM 330V340 80KM&B-430&D3048 CLR 20/15 Q0989",
      "????&A0 262026Z 30111KT&D900NG 299V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 8CI329&CI001FNMN000L 17/11 Q0999",
      "????&A0 262028Z 30110KT&D0NG 300V302 80KM&B-394&D3048 2ST016&ST000FNVN000N 1CU034&CU001FLLN000N 11/10 Q0995",
      "????&A0 262028Z 30110KT&D0NG 300V302 80KM&B-394&D3048 CLR 17/11 Q0999",
      "????&A0 262029Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 12/09 Q0990",
      "????&A0 262029Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU036&CU000FMVN000N 6CI329&CI001FNMN000L 20/12 Q1009",
      "????&A0 262029Z 30110KT&D900NG 299V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 8CI329&CI001FNMN000L 18/12 Q0999",
      "????&A0 262029Z 30115KT&D900NG 298V303 80KM&B-394&D3048 1CU034&CU001FLLN000N 8CI329&CI001FNMN000L 13/09 Q0999",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU025&CU000FLVN000N 6CI329&CI001FNMN000L 19/12 Q1008",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU027&CU000FMVN000N 6CI329&CI001FNMN000L 20/12 Q1010",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 10/08 Q0989",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 6CI329&CI001FNMN000L 19/12 Q1008",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 6CI329&CI001FNMN000L 19/12 Q1010",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 6CI329&CI001FNMN000L 20/12 Q1010",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 1ST016&ST000FLVN000N 6CI329&CI001FNMN000L 13/09 Q0992",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 3CU029&CU000FLVN000N 6CI329&CI001FNMN000L 19/12 Q1010",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 3CU034&CU001FLLN000N 6CI329&CI001FNMN000L 20/12 Q1010",
      "????&A0 262036Z 30110KT&D0NG 300V302 80KM&B-394&D3048 6CI329&CI001FNMN000L 19/12 Q1010",
      "????&A0 262036Z 30111KT&D900NG 299V302 80KM&B-394&D3048 1CU034&CU001FLLN000N 8CI329&CI001FNMN000L 17/11 Q0999",
      "????&A0 262037Z 24705G06KT&D975NG 100KM&B-457&D30936 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 15/14 Q1009",
      "????&A0 262037Z 24705G06KT&D975NG 100KM&B-457&D30936 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 16/15 Q1009",
      "????&A0 262037Z 24705G06KT&D975NG 48KM&B79&D3500 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 15/14 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 2CU016&CU000FLVN000N 12/11 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 2CU016&CU000FLVN000N 13/12 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 2ST025&ST001FNVN002N 13/12 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 2ST025&ST001FNVN002N 15/14 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 2ST025&ST001FNVN002N 1CI312&CI001FNVN002N 13/12 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 4ST025&ST001FNVR002N 1CI312&CI001FNVN002N 12/11 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 CLR 09/08 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 CLR 12/11 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 CLR 13/12 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 CLR 14/13 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 100KM&B-457&D30936 CLR 16/15 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 16KM&B79&D3500 8ST025&ST001FNMR002N 2CU041&CU001FNVR002N 5CI312&CI001FNVN002N 14/13 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 32KM&B79&D3500 4ST025&ST001FNVR002N 2CU041&CU001FNVR002N 6CI312&CI001FNVN002N 14/13 Q1009",
      "????&A0 262038Z 24705G06KT&D975NG 48KM&B79&D3500 6ST025&ST001FNLR002N 2CU041&CU001FNVR002N 3CI312&CI001FNVN002N 14/13 Q1009",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 4ST016&ST000FNVN000N 7ST047&ST001FNHS000L 00/-2 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 5ST047&ST001FNHS000L -3/-5 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L 00/-2 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -1/-3 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -2/-4 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -3/-13 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -4/-6 Q1001",
      "????&A0 262040Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -8/-18 Q1001",
      "????&A0 262040Z 05417G21KT&D985NG 043V066 100KM&B-457&D30936 7ST047&ST001FNHS000L -15/-17 Q1001",
      "????&A0 262041Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -8/-18 Q1001",
      "????&A0 262041Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -8/-9 Q1001",
      "????&A0 262041Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -9/-10 Q1001",
      "????&A0 262041Z 04612G15KT&D985NG 036V056 32KM&B-9&D2000 7ST047&ST001FNHS000L -9/-9 Q1001",
      "????&A0 262041Z 05014G18KT&D985NG 039V061 32KM&B-9&D2000 7ST047&ST001FNHS000L -12/-14 Q1001",
      "????&A0 262042Z 32514G23KT&D980MM 320V331 80KM&B-371&D3048 8CB018&CB001FSDR000N 18/13 Q0989",
      "????&A0 262042Z 32518G28KT&D980MM 320V330 80KM&B-371&D3048 8CB018&CB001FSDR000N 4CI330&CI001FMVN000N 18/16 Q0989",
      "????&A0 262042Z 33514G23KT&D980MM 330V340 80KM&B-371&D3048 CLR 15/10 Q0989",
      "????&A0 262042Z 33514G23KT&D980MM 330V340 80KM&B-371&D3048 CLR 17/12 Q0989",
      "????&A0 262042Z 33514G23KT&D980MM 330V340 80KM&B-371&D3048 CLR 19/14 Q0989",
      "????&A0 262042Z 33514G23KT&D980MM 330V340 80KM&B-371&D3048 CLR 20/15 Q0989",
      "????&A0 262042Z 34418G28KT&D980HM 333V354 80KM&B-371&D3048 8CB018&CB001FSDR000N 18/16 Q0989",
      "????&A0 262043Z 32518G28KT&D980MM 320V330 80KM&B-371&D3048 8CB018&CB001FSDR000N 4CI330&CI001FMVN000N 19/17 Q0989",
      "????&A0 262043Z 32518G28KT&D980MM 320V330 80KM&B-371&D3048 8CB018&CB001FSDR000N 4CI330&CI001FMVN000N 20/18 Q0989",
      "????&A0 262043Z 33514G23KT&D980MM 330V340 80KM&B-371&D3048 CLR 19/14 Q0989",
      "????&A0 262043Z 33514G23KT&D980MM 330V340 80KM&B-371&D3048 CLR 20/15 Q0989"
    });

  for(QString m : metars)
  {
    Metar debugmetar(m, "XXXX", QDateTime::currentDateTime().toUTC(), true);
    const atools::fs::weather::MetarParser& parsedMetar = debugmetar.getParsedMetar();

    if(parsedMetar.isValid() && parsedMetar.getUnusedData().isEmpty())
    {
      qDebug() << "=========================================================";
      qDebug() << "RAW   " << m;
      qDebug() << "CLEAN " << debugmetar.getCleanMetar();
      qDebug() << "unused" << parsedMetar.getUnusedData();
      qDebug() << "weather" << parsedMetar.getWeather();

      qDebug() << "mbar" << parsedMetar.getPressureMbar();
      qDebug() << "wind" << parsedMetar.getWindDir();
      qDebug() << "wind" << parsedMetar.getWindSpeedMeterPerSec();
      qDebug() << "gust" << parsedMetar.getGustSpeedMeterPerSec();
      qDebug() << "temp" << parsedMetar.getTemperatureC();
      qDebug() << "dew" << parsedMetar.getDewpointDegC();
    }
    else
    {
      qDebug() << "INVALID";
      qDebug() << "RAW   " << m;
      qDebug() << "CLEAN " << debugmetar.getCleanMetar();

      Metar m2(m, "XXXX", QDateTime::currentDateTime().toUTC(), true);
      const atools::fs::weather::MetarParser& pm2 = m2.getParsedMetar();
      Q_UNUSED(pm2);
    }
  }
}

} // namespace weather
} // namespace fs
} // namespace atools