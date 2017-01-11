-- *****************************************************************************
-- Copyright 2015-2017 Alexander Barthel albar965@mailbox.org
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.
-- ****************************************************************************/

-- *************************************************************
-- Drop all airport facility tables
-- *************************************************************

-- Order is important to avoid fk conflicts

-- drop airport facilities
drop table if exists delete_airport;
drop table if exists parking;
drop table if exists taxi_path;
drop table if exists fence;
drop table if exists apron_light;
drop table if exists apron;
drop table if exists start;
drop table if exists helipad;
drop table if exists com;

