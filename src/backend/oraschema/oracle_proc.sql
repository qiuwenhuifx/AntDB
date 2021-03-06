/*
 * Oracle Functions
 *
 * Copyright (c) 1996-2013, PostgreSQL Global Development Group
 * Copyright (c) 2014-2016, ADB Development Group
 *
 * src/backend/oraschema/oracle_proc.sql
 */

/*
 * Function: bitand
 * Parameter Type: : (numeric, numeric)
 */
CREATE OR REPLACE FUNCTION oracle.bitand(bigint, bigint)
    RETURNS bigint
    AS $$select $1 & $2;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: bitand
 * Parameter Type: : (numeric, numeric)
 */
CREATE OR REPLACE FUNCTION oracle.bitand(numeric, numeric)
    RETURNS bigint
    AS $$select trunc($1)::bigint & trunc($2)::bigint;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: nanvl
 * Parameter Type: (float8, float8)
 */
CREATE OR REPLACE FUNCTION oracle.nanvl(float8, float8)
    RETURNS float8
    AS $$SELECT CASE WHEN $1 = 'NaN' THEN $2 ELSE $1 END;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: nanvl
 * Parameter Type: (numeric, numeric)
 */
CREATE OR REPLACE FUNCTION oracle.nanvl(numeric, numeric)
    RETURNS numeric
    AS $$SELECT CASE WHEN $1 = 'NaN' THEN $2 ELSE $1 END;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: sinh
 * sinh x = (e ^ x - e ^ (-x))/2
 * Parameter Type: : (numeric)
 */
CREATE OR REPLACE FUNCTION oracle.sinh(numeric)
    RETURNS numeric
    AS $$SELECT (exp($1) - exp(-$1)) / 2;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: sinh
 * sinh x = (e ^ x - e ^ (-x))/2
 * Parameter Type: : (float8)
 */
CREATE OR REPLACE FUNCTION oracle.sinh(float8)
    RETURNS float8
    AS $$SELECT (exp($1) - exp(-$1)) / 2;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: cosh
 * cosh x = (e ^ x + e ^ (-x))/2
 * Parameter Type: (numeric)
 */
CREATE OR REPLACE FUNCTION oracle.cosh(numeric)
    RETURNS numeric
    AS $$SELECT (exp($1) + exp(-$1)) / 2;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: cosh
 * cosh x = (e ^ x + e ^ (-x))/2
 * Parameter Type: (float8)
 */
CREATE OR REPLACE FUNCTION oracle.cosh(float8)
    RETURNS float8
    AS $$SELECT (exp($1) + exp(-$1)) / 2;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: tanh
 * tanh x = sinh x / cosh x = (e ^ x - e ^ (-x)) / (e ^ x + e ^ (-x))
 * Parameter Type: (numeric)
 */
CREATE OR REPLACE FUNCTION oracle.tanh(numeric)
    RETURNS numeric
    AS $$SELECT (exp($1) - exp(-$1)) / (exp($1) + exp(-$1));$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: tanh
 * tanh x = sinh x / cosh x = (e ^ x - e ^ (-x)) / (e ^ x + e ^ (-x))
 * Parameter Type: (float8)
 */
CREATE OR REPLACE FUNCTION oracle.tanh(float8)
    RETURNS float8
    AS $$SELECT (exp($1) - exp(-$1)) / (exp($1) + exp(-$1));$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: INSTR
 */
CREATE OR REPLACE FUNCTION oracle.instr(str text, patt text, start int default 1, nth int default 1)
    RETURNS int
    AS 'oratext_instr4'
    LANGUAGE INTERNAL
    PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.to_timestamp(text)
    RETURNS timestamp
    AS $$SELECT $1::timestamp;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: ADD_MONTHS
 */
CREATE OR REPLACE FUNCTION oracle.add_months(TIMESTAMP WITH TIME ZONE, INTEGER)
    RETURNS TIMESTAMP
    AS $$SELECT oracle.add_months($1::pg_catalog.date, $2) + $1::pg_catalog.time;$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: ADD_MONTHS
 */
CREATE OR REPLACE FUNCTION oracle.add_months(TIMESTAMP WITH TIME ZONE, NUMERIC)
    RETURNS TIMESTAMP
    AS $$SELECT oracle.add_months($1::pg_catalog.date, trunc($2)::int4) + $1::pg_catalog.time;$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: LAST_DAY
 */
CREATE OR REPLACE FUNCTION oracle.last_day(TIMESTAMP WITH TIME ZONE)
    RETURNS oracle.date
    AS $$SELECT (oracle.last_day($1::pg_catalog.date) + $1::time)::oracle.date;$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: months_between
 */
CREATE OR REPLACE FUNCTION oracle.months_between(TIMESTAMP WITH TIME ZONE, TIMESTAMP WITH TIME ZONE)
    RETURNS NUMERIC
    AS $$SELECT oracle.months_between($1::pg_catalog.date, $2::pg_catalog.date);$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

CREATE OR REPLACE FUNCTION oracle.months_between(TIMESTAMP WITH TIME ZONE, oracle.varchar2)
    RETURNS NUMERIC
    AS $$SELECT oracle.months_between($1::pg_catalog.date, $2::pg_catalog.date);$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: new_time
 * Parameter Type: (timestamp, text, text)
 */
CREATE OR REPLACE FUNCTION oracle.new_time(tt timestamp with time zone, z1 text, z2 text)
    RETURNS timestamp
    AS $$
    DECLARE
    src_interval INTERVAL;
    dst_interval INTERVAL;
    BEGIN
        SELECT utc_offset INTO src_interval FROM pg_timezone_abbrevs WHERE abbrev = z1;
        IF NOT FOUND THEN
            RAISE EXCEPTION 'Invalid time zone: %', z1;
        END IF;
        SELECT utc_offset INTO dst_interval FROM pg_timezone_abbrevs WHERE abbrev = z2;
        IF NOT FOUND THEN
            RAISE EXCEPTION 'Invalid time zone: %', z2;
        END IF;
        RETURN tt - src_interval + dst_interval;
    END;
    $$
    LANGUAGE plpgsql
    STABLE PARALLEL SAFE
    STRICT;

/*
 * Function: next_day
 * Parameter Type: (oracle.date, text)
 * Parameter Type: (timestamptz, text)
 */
CREATE OR REPLACE FUNCTION oracle.next_day(oracle.date, text)
    RETURNS oracle.date
    AS $$SELECT (oracle.ora_next_day($1::date, $2) + $1::time)::oracle.date;$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;
CREATE OR REPLACE FUNCTION oracle.next_day(timestamptz, text)
    RETURNS oracle.date
    AS $$SELECT (oracle.ora_next_day($1::date, $2) + $1::time)::oracle.date;$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: round
 */
CREATE OR REPLACE FUNCTION oracle.round(pg_catalog.date, text default 'DDD')
    RETURNS pg_catalog.date
    AS 'ora_date_round'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

CREATE OR REPLACE FUNCTION oracle.round(timestamptz, text default 'DDD')
    RETURNS oracle.date
    AS $$select oracle.ora_timestamptz_round($1, $2)::oracle.date;$$
    LANGUAGE SQL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: trunc
 * Parameter Type: (date, text)
 * Parameter Type: (date)
 * Parameter Type: (timestamp with time zone, text)
 * Parameter Type: (timestamp with time zone)
 */
CREATE OR REPLACE FUNCTION oracle.trunc(pg_catalog.date, text default 'DDD')
    RETURNS pg_catalog.date
    AS 'ora_pg_date_trunc'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

CREATE OR REPLACE FUNCTION oracle.trunc(oracle.date, text default 'DDD')
    RETURNS oracle.date
    AS 'ora_oracle_date_trunc'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

CREATE OR REPLACE FUNCTION oracle.trunc(pg_catalog.timestamptz, text default 'DDD')
    RETURNS pg_catalog.timestamptz
    AS 'ora_timestamptz_trunc'
    LANGUAGE INTERNAL
    STABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: first
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.first(str text)
    RETURNS text
    AS 'orachr_first'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: last
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.last(str text)
    RETURNS text
    AS 'orachr_last'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: to_char
 * Parameter Type: (smallint)
 * Parameter Type: (smallint, text)
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.to_char(smallint)
    RETURNS text
    AS $$select oracle.to_char($1::int4)$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;
CREATE OR REPLACE FUNCTION oracle.to_char(smallint, text)
    RETURNS text
    AS $$select oracle.to_char($1::int4, $2)$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;
CREATE FUNCTION oracle.to_char(text)
    RETURNS TEXT
    AS 'select $1'
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: to_number
 * Parameter Type: (float8)
 * Parameter Type: (float8, text)
 * Parameter Type: (numeric)
 * Parameter Type: (numeric, text)
 */
CREATE OR REPLACE FUNCTION oracle.to_number(numeric)
    RETURNS numeric
    AS 'select $1'
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;
CREATE OR REPLACE FUNCTION oracle.to_number(numeric, text)
    RETURNS numeric
    AS 'select oracle.to_number($1::text, $2)'
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: to_multi_byte
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.to_multi_byte(text)
    RETURNS text
    AS 'ora_to_multi_byte'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: to_single_byte
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.to_single_byte(text)
    RETURNS text
    AS 'ora_to_single_byte'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: substr
 * Parameter Type: (text, int)
 * Parameter Type: (text, int, int)
 * Parameter Type: (numeric, numeric)
 * Parameter Type: (numeric, numeric, numeric)
 * Parameter Type: (varchar, numeric)
 * Parameter Type: (varchar, numeric, numeric)
 */
CREATE OR REPLACE FUNCTION oracle.substr(text, integer)
    RETURNS text
    AS 'orastr_substr2'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

CREATE OR REPLACE FUNCTION oracle.substr(text, integer, integer)
    RETURNS text
    AS 'orastr_substr3'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: dump
 * Parameter Type: (text, int)
 */
CREATE OR REPLACE FUNCTION oracle.dump(text, integer default 10)
    RETURNS varchar
    AS 'ora_dump'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: length
 * Parameter Type: (char)
 * Parameter Type: (varchar)
 * Parameter Type: (varchar2)
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.length(char)
    RETURNS integer
    AS 'orastr_bpcharlen'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: lengthb
 * Parameter Type: (varchar2)
 * Parameter Type: (varchar)
 * Parameter Type: (text)
 */
CREATE OR REPLACE FUNCTION oracle.lengthb(oracle.varchar2)
    RETURNS integer
    AS 'ora_byteaoctetlen'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;


/*
 * Function: remainder
 * Parameter Type: (numeric, numeric)
 */
CREATE OR REPLACE FUNCTION oracle.remainder(n2 numeric, n1 numeric)
    RETURNS numeric
    AS $$select n2 - n1*((dround(n2::float8/n1::float8))::numeric);$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: sys_extract_utc
 */
CREATE OR REPLACE FUNCTION oracle.sys_extract_utc(timestamp with time zone)
    RETURNS timestamp
    AS $$select $1 at time zone 'UTC';$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    STRICT;

/*
 * Function: mode
 * Parameter Type: (numeric, numeric)
 * Add oracle.mod(numeric, numeric) to make sure find oracle.mod if
 * current grammar is oracle;
 */
CREATE OR REPLACE FUNCTION oracle.mod(numeric, numeric)
    RETURNS numeric
    AS 'numeric_mod'
    LANGUAGE INTERNAL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    RETURNS NULL ON NULL INPUT;

/*
 * Function: replace(text, text)
 */
CREATE OR REPLACE FUNCTION oracle.replace(text, text)
    RETURNS text
    AS $$select oracle.replace($1, $2, NULL);$$
    LANGUAGE SQL
    IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
    CALLED ON NULL INPUT;

/*
 * dbms_output schema
 * add put_line function
 */
create schema IF NOT EXISTS dbms_output;
GRANT USAGE ON SCHEMA dbms_output TO PUBLIC;

create or replace function dbms_output.put_line(putout in text)
RETURNS void AS $$
  DECLARE
    ret_val text;
  BEGIN
    ret_val:=putout;
    RAISE NOTICE  '%',ret_val;
  end;
  $$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  LANGUAGE PLPGSQL;

create schema IF NOT EXISTS  dbms_lock;
GRANT USAGE ON SCHEMA dbms_lock TO PUBLIC;
create or replace function dbms_lock.sleep(sleep_second in double precision) 
  RETURNS void  AS
    'select pg_sleep($1);'
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION round(interval, int default 0)
RETURNS float8 AS 
$$select round(EXTRACT(EPOCH FROM $1)::numeric/86400, $2)::float8;$$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION trunc(interval, int default 0)
RETURNS float8 AS
  $$ select trunc(EXTRACT(EPOCH FROM $1)::numeric/86400, $2)::float8; $$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION sign(interval)
RETURNS float8 AS 
  $$ select sign(EXTRACT(EPOCH FROM $1)::numeric/86400)::float8; $$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION mod(text, numeric) RETURNS numeric AS
  $$ select mod($1::numeric, $2); $$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
LANGUAGE SQL;

CREATE OR REPLACE FUNCTION oracle.is_prefix(varchar, varchar, boolean DEFAULT true)
RETURNS boolean AS
  $$orastr_is_prefix_text$$
  IMMUTABLE PARALLEL SAFE STRICT
--ADBONLY CLUSTER SAFE
LANGUAGE INTERNAL;

CREATE OR REPLACE FUNCTION oracle.rvrs(varchar, integer DEFAULT 1, integer DEFAULT NULL)
RETURNS varchar AS
  $$orastr_rvrs$$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
CALLED ON NULL INPUT
LANGUAGE INTERNAL;

CREATE OR REPLACE FUNCTION oracle.swap(varchar, varchar, integer DEFAULT 1, integer DEFAULT NULL)
RETURNS varchar AS
  $$orastr_swap$$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
CALLED ON NULL INPUT
LANGUAGE INTERNAL;

CREATE OR REPLACE FUNCTION oracle.betwn(varchar, integer, integer, boolean DEFAULT true)
RETURNS varchar AS
  $$orastr_betwn_i$$
  IMMUTABLE PARALLEL SAFE STRICT
--ADBONLY CLUSTER SAFE
LANGUAGE INTERNAL;

CREATE OR REPLACE FUNCTION oracle.betwn(varchar, varchar,
  varchar DEFAULT NULL,
  INTEGER DEFAULT 1,
  INTEGER DEFAULT 1,
  BOOLEAN DEFAULT true,
  BOOLEAN DEFAULT false)
RETURNS varchar AS
  $$orastr_betwn_c$$
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
LANGUAGE INTERNAL;

CREATE OR REPLACE FUNCTION oracle.lpad(text, int4, text default ' ')
  RETURNS text
  AS $$ora_lpad$$
  LANGUAGE INTERNAL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.lpad(text, int8, text default ' ')
  RETURNS text
  AS $$select oracle.lpad($1, pg_catalog.int4($2), $3)$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.lpad(text, numeric, text default ' ')
  RETURNS text
  AS $$select oracle.lpad($1, pg_catalog.int4(pg_catalog.trunc($2, 0)), $3)$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.rpad(text, int4, text default ' ')
  RETURNS text
  AS $$ora_rpad$$
  LANGUAGE INTERNAL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.rpad(text, int8, text default ' ')
  RETURNS text
  AS $$select oracle.rpad($1, pg_catalog.int4($2), $3)$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.rpad(text, numeric, text default ' ')
  RETURNS text
  AS $$select oracle.rpad($1, pg_catalog.int4(pg_catalog.trunc($2, 0)), $3)$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.width_bucket(float8, float8, float8, numeric)
  RETURNS int4
  AS $$select pg_catalog.width_bucket($1, $2, $3, pg_catalog.int4(pg_catalog.trunc($4, 0)))$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.width_bucket(numeric, numeric, numeric, numeric)
  RETURNS int4
  AS $$select pg_catalog.width_bucket($1, $2, $3, pg_catalog.int4(pg_catalog.trunc($4, 0)))$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.trunc(numeric, numeric)
  RETURNS NUMERIC
  AS $$select pg_catalog.trunc($1, pg_catalog.int4(pg_catalog.trunc($2, 0)))$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;

CREATE OR REPLACE FUNCTION oracle.round(numeric, numeric)
  RETURNS NUMERIC
  AS $$select pg_catalog.round($1, pg_catalog.int4(pg_catalog.trunc($2, 0)))$$
  LANGUAGE SQL
  IMMUTABLE PARALLEL SAFE
--ADBONLY CLUSTER SAFE
  STRICT;
