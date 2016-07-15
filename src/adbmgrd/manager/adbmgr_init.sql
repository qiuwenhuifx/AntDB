--views
CREATE VIEW adbmgr.host AS
  SELECT
    hostname AS name,
    hostuser AS user,
    hostport AS port,
    CASE hostproto
      WHEN 's' THEN 'ssh'::text
      WHEN 't' THEN 'telnet'::text
    END AS protocol,
    hostagentport  AS agentport,
    hostaddr AS address,
    hostpghome AS pghome
  FROM pg_catalog.mgr_host order by 1;

CREATE VIEW adbmgr.parm AS
  SELECT
    parmnode       AS  node,
    parmkey        AS  key,
    parmvalue      AS  value,
    parmconfigtype AS  configtype,
    parmcomment	   AS  comment
  FROM pg_catalog.mgr_parm;

CREATE VIEW adbmgr.node AS
  SELECT
    mgrnode.nodename    AS  name,
    hostname   AS  host,
    CASE mgrnode.nodetype
      WHEN 'g' THEN 'gtm master'::text
      WHEN 'p' THEN 'gtm slave'::text
      WHEN 'c' THEN 'coordinator'::text
      WHEN 's' THEN 'coordinator slave'::text
      WHEN 'd' THEN 'datanode master'::text
      WHEN 'b' THEN 'datanode slave'::text
    END AS type,
    node_alise.nodename AS mastername,
    mgrnode.nodeport    AS  port,
    mgrnode.nodepath    AS  path,
    mgrnode.nodeprimary AS primary,
    mgrnode.nodeinited  AS  initialized,
    mgrnode.nodeincluster AS incluster
  FROM pg_catalog.mgr_node AS mgrnode LEFT JOIN pg_catalog.mgr_host ON mgrnode.nodehost = pg_catalog.mgr_host.oid LEFT JOIN pg_catalog.mgr_node AS node_alise
  ON node_alise.oid = mgrnode.nodemasternameoid order by 3;

CREATE VIEW adbmgr.monitor_datanode_all AS
	select * from mgr_monitor_dnmaster_all()
	union all
	select * from mgr_monitor_dnslave_all();

--init all
CREATE VIEW adbmgr.initall AS
	SELECT 'init gtm master' AS "operation type",* FROM mgr_init_gtm_master(NULL)
	UNION ALL
	SELECT 'start gtm master' AS "operation type", * FROM mgr_start_gtm_master(NULL)
	UNION ALL
	SELECT 'init coordinator' AS "operation type",* FROM mgr_init_cn_master(NULL)
	UNION ALL
	SELECT 'start coordinator' AS "operation type", * FROM mgr_start_cn_master(NULL)
	UNION ALL
	SELECT 'init datanode master' AS "operation type", * FROM mgr_init_dn_master(NULL)
	UNION ALL
	SELECT 'start datanode master' AS "operation type", * FROM mgr_start_dn_master(NULL)
	UNION ALL
	SELECT 'init datanode slave' AS "operation type", * FROM mgr_init_dn_slave_all()
	UNION ALL
	SELECT 'start datanode slave' AS "operation type", * FROM mgr_start_dn_slave(NULL)
	UNION ALL
	SELECT 'config coordinator' AS "operation type", * FROM mgr_configure_nodes_all(NULL);

--init datanode all
CREATE VIEW adbmgr.initdatanodeall AS
    SELECT 'init datanode master' AS "operation type",* FROM mgr_init_dn_master(NULL)
    UNION all
    SELECT 'init datanode slave' AS "operation type", * FROM mgr_init_dn_slave_all();
--start datanode all
CREATE VIEW adbmgr.start_datanode_all AS
    SELECT 'start datanode master' AS "operation type", * FROM mgr_start_dn_master(NULL)
    UNION all
    SELECT 'start datanode slave' AS "operation type", * FROM mgr_start_dn_slave(NULL);
--start all
CREATE VIEW adbmgr.startall AS
    SELECT 'start gtm master' AS "operation type", * FROM mgr_start_gtm_master(NULL)
    UNION ALL
    SELECT 'start coordinator' AS "operation type", * FROM mgr_start_cn_master(NULL)
    UNION all
    SELECT 'start datanode master' AS "operation type", * FROM mgr_start_dn_master(NULL)
    UNION all
    SELECT 'start datanode slave' AS "operation type", * FROM mgr_start_dn_slave(NULL);
--stop datanode all
CREATE VIEW adbmgr.stop_datanode_all AS
    SELECT 'stop datanode master' AS "operation type", * FROM mgr_stop_dn_master(NULL)
    UNION all
    SELECT 'stop datanode slave' AS "operation type", * FROM mgr_stop_dn_slave(NULL);

CREATE VIEW adbmgr.stop_datanode_all_f AS
    SELECT 'stop datanode master' AS "operation type", * FROM mgr_stop_dn_master_f(NULL)
    UNION all
    SELECT 'stop datanode slave' AS "operation type", * FROM mgr_stop_dn_slave_f(NULL);

CREATE VIEW adbmgr.stop_datanode_all_i AS
    SELECT 'stop datanode master' AS "operation type", * FROM mgr_stop_dn_master_i(NULL)
    UNION all
    SELECT 'stop datanode slave' AS "operation type", * FROM mgr_stop_dn_slave_i(NULL);

--stop all
CREATE VIEW adbmgr.stopall AS
    SELECT 'stop coordinator' AS "operation type", * FROM mgr_stop_cn_master(NULL)
    UNION all
    SELECT 'stop datanode master' AS "operation type", * FROM mgr_stop_dn_master(NULL)
    UNION all
    SELECT 'stop datanode slave' AS "operation type", * FROM mgr_stop_dn_slave(NULL)
    UNION ALL
    SELECT 'stop gtm master' AS "operation type", * FROM mgr_stop_gtm_master(NULL);

CREATE VIEW adbmgr.stopall_f AS
    SELECT 'stop coordinator' AS "operation type", * FROM mgr_stop_cn_master_f(NULL)
    UNION all
    SELECT 'stop datanode master' AS "operation type", * FROM mgr_stop_dn_master_f(NULL)
    UNION all
    SELECT 'stop datanode slave' AS "operation type", * FROM mgr_stop_dn_slave_f(NULL)
    UNION ALL
    SELECT 'stop gtm master' AS "operation type", * FROM mgr_stop_gtm_master_f(NULL);

CREATE VIEW adbmgr.stopall_i AS
    SELECT 'stop coordinator' AS "operation type", * FROM mgr_stop_cn_master_i(NULL)
    UNION all
    SELECT 'stop datanode master' AS "operation type", * FROM mgr_stop_dn_master_i(NULL)
    UNION all
    SELECT 'stop datanode slave' AS "operation type", * FROM mgr_stop_dn_slave_i(NULL)
    UNION ALL
    SELECT 'stop gtm master' AS "operation type", * FROM mgr_stop_gtm_master_i(NULL);

-- for ADB monitor host page: get all host various parameters.
CREATE VIEW adbmgr.get_all_host_parm AS
    select 
        mgh.hostname AS hostName, 
        mgh.hostaddr AS ip,
        nh.mh_cpu_core_available AS cpu,
        round(c.mc_cpu_usage::numeric, 1) AS cpuRate,
        round((m.mm_total/1024.0/1024.0/1024.0)::numeric, 1) AS mem,
        round(m.mm_usage::numeric, 1) AS memRate,
        round((d.md_total/1024.0/1024.0/1024.0)::numeric, 1) AS disk,
        round(((d.md_used/d.md_total::float) * 100)::numeric, 1) AS diskRate,
        mtc.mt_emergency_threshold AS cpu_threshold,
        mtm.mt_emergency_threshold AS mem_threshold,
        mtd.mt_emergency_threshold AS disk_threshold
    from 
        mgr_host mgh,
        
        (
            select * from (
                            select *, (ROW_NUMBER()OVER(PARTITION BY T.host_oid ORDER BY T.mc_timestamptz desc)) as rm
                            from monitor_cpu t
                           ) tt where tt.rm = 1
        ) c,
        
        (
            select * from (
                            select *,(ROW_NUMBER()OVER(PARTITION BY T.host_oid ORDER BY T.mm_timestamptz desc)) as rm
                            from monitor_mem t
                           ) tt where tt.rm =1
        ) m,
        
        (
            select * from (
                            select *, (ROW_NUMBER()OVER(PARTITION BY T.host_oid ORDER BY T.md_timestamptz desc)) as rm
                            from monitor_disk t
                           ) tt where tt.rm = 1
        ) d,
        
        (
            select * from (
                            select *, (ROW_NUMBER()OVER(PARTITION BY T.host_oid ORDER BY T.mh_current_time desc)) as rm
                            from monitor_host t
                           ) tt where tt.rm = 1
        ) nh,
        
        (
            select * from monitor_host_threshold where mt_type = 1
        ) mtc,
        
        (
            select * from monitor_host_threshold where mt_type = 2
        ) mtm,
        
        (
            select * from monitor_host_threshold where mt_type = 3
        )mtd
    where mgh.oid = c.host_oid and
        c.host_oid = m.host_oid and
        m.host_oid = d.host_oid and
        d.host_oid = nh.host_oid;

-- for ADB monitor host page: get specific host various parameters.
CREATE VIEW adbmgr.get_spec_host_parm AS
    select mgh.hostname as hostname,
       mgh.hostaddr as ip,
       mh.mh_system as os,
       mh.mh_cpu_core_available as cpu,
       round((mm.mm_total/1024.0/1024.0/1024.0)::numeric, 1) as mem,
       round((md.md_total/1024.0/1024.0/1024.0)::numeric, 1) as disk,
       mh.mh_current_time - (mh.mh_seconds_since_boot || 'sec')::interval as createtime,
       mh_run_state as state,
       round(mc.mc_cpu_usage::numeric, 1) as cpurate,
       round(mm.mm_usage::numeric, 1) as memrate,
       round(((md.md_used/md.md_total::float) * 100)::numeric, 1) as diskRate,
       round((md.md_io_read_bytes/1024.0/1024.0)/(md.md_io_read_time/1000.0), 1) as ioreadps,
       round((md.md_io_write_bytes/1024.0/1024.0)/(md.md_io_write_time/1000.0), 1) as iowriteps,
       round(mn.mn_recv/1024.0,1) as netinps,
       round(mn.mn_sent/1024.0,1) as netoutps,
       mh.mh_seconds_since_boot as runtime
    from mgr_host mgh,
    
        (
            select * from (
                            select *, (ROW_NUMBER()OVER(PARTITION BY t.host_oid ORDER BY t.mh_current_time desc)) as rm
                            from monitor_host t
                           ) tt where tt.rm = 1
        ) mh,
        
        (
            select * from (
                            select *,(ROW_NUMBER()OVER(PARTITION BY t.host_oid ORDER BY t.mc_timestamptz desc)) as rm
                            from monitor_cpu t
                           ) tt where tt.rm = 1
        ) mc,
        
        (
            select * from (
                            select *,(ROW_NUMBER()OVER(PARTITION BY t.host_oid ORDER BY t.mm_timestamptz desc)) as rm
                            from monitor_mem t
                           ) tt where tt.rm =1
        ) mm,
        
        (
            select * from (
                            select *, (ROW_NUMBER()OVER(PARTITION BY t.host_oid ORDER BY t.md_timestamptz desc)) as rm
                            from monitor_disk t
                           ) tt where tt.rm = 1
        ) md,
        
        (
            select * from (
                            select *, (ROW_NUMBER()OVER(PARTITION BY t.host_oid ORDER BY t.mn_timestamptz desc)) as rm
                            from monitor_net t
                           ) tt where tt.rm = 1
        ) mn
    where mgh.oid = mh.host_oid and
        mh.host_oid = mc.host_oid and
        mc.host_oid = mm.host_oid and
        mm.host_oid = md.host_oid and
        md.host_oid = mn.host_oid;

-- for ADB monitor host page: get cpu, memory, i/o and net info for specific time period.
CREATE OR REPLACE FUNCTION pg_catalog.get_host_history_usage(hostname text, i int)
    RETURNS table 
    (
        recordtimes timestamptz,
        cpuuseds numeric,
        memuseds numeric,
        ioreadps numeric,
        iowriteps numeric,
        netinps numeric,
        netoutps numeric
    )
    AS 
    $$
    
    select c.mc_timestamptz as recordtimes,
           round(c.mc_cpu_usage::numeric, 1) as cpuuseds,
           round(m.mm_usage::numeric, 1) as memuseds,
           round((d.md_io_read_bytes/1024.0/1024.0)/(d.md_io_read_time/1000.0), 1) as ioreadps,
           round((d.md_io_write_bytes/1024.0/1024.0)/(d.md_io_write_time/1000.0), 1) as iowriteps,
           round(n.mn_recv/1024.0,1) as netinps,
           round(n.mn_sent/1024.0,1) as netoutps
    from monitor_cpu c left join monitor_mem  m on(c.host_oid = m.host_oid and c.mc_timestamptz = m.mm_timestamptz)
                       left join monitor_disk d on(c.host_oid = d.host_oid and c.mc_timestamptz = d.md_timestamptz)
                       left join monitor_net  n on(c.host_oid = n.host_oid and c.mc_timestamptz = n.mn_timestamptz)
                       left join monitor_host h on(c.host_oid = h.host_oid and c.mc_timestamptz = h.mh_current_time)
                       left join mgr_host   mgr on(c.host_oid = mgr.oid)
                       left join (select mh.host_oid, mh.mh_current_time 
                                  from monitor_host mh 
                                  order by mh.mh_current_time desc 
                                  limit 1) temp on(c.host_oid = temp.host_oid)
    where c.mc_timestamptz  >  temp.mh_current_time - case $2 
                                                      when 0 then interval '1 hour'
                                                      when 1 then interval '1 day'
                                                      when 2 then interval '7 day'
                                                      end and
          mgr.hostname = $1;
    $$
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

-- for ADB monitor host page: The names of all the nodes on a host
CREATE OR REPLACE FUNCTION pg_catalog.get_all_nodename_in_spec_host(hostname text)
    RETURNS table (all_nodename name)
    AS 
    $$

    select nodename as all_node_name
    from mgr_node
    where nodehost = (select oid from mgr_host where hostname = $1);

    $$
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

--make view for 12 hours data for  tps, qps, connectnum, dbsize, indexsize
CREATE VIEW adbmgr.monitor_cluster_fouritem_v AS 
SELECT a.monitor_databasetps_time::timestamptz(0) AS time , a.tps, a.qps, b.connectnum, b.dbsize, b.indexsize
FROM
	(SELECT monitor_databasetps_time, sum(monitor_databasetps_tps) AS tps , 
	sum(monitor_databasetps_qps) AS qps  , row_number() over (PARTITION BY 1) FROM (SELECT 
	distinct(monitor_databasetps_dbname),  monitor_databasetps_time, monitor_databasetps_tps,
	monitor_databasetps_qps, monitor_databasetps_runtime FROM monitor_databasetps WHERE 
	monitor_databasetps_time > (SELECT max(monitor_databasetps_time) - interval '12 hour' FROM 
	monitor_databasetps ) ORDER BY 2 ASC)AS a  GROUP BY monitor_databasetps_time) AS a
JOIN 
	(SELECT  monitor_databaseitem_time, sum(monitor_databaseitem_connectnum) AS connectnum, 
	(sum(monitor_databaseitem_dbsize)/1024.0)::numeric(18,2) AS dbsize , (sum(monitor_databaseitem_indexsize)/1024.0)::numeric(18,2) AS indexsize, row_number() over (PARTITION BY 1) FROM (SELECT 
	distinct(monitor_databaseitem_dbname),  monitor_databaseitem_time, monitor_databaseitem_connectnum, 
	monitor_databaseitem_dbsize, monitor_databaseitem_indexsize FROM monitor_databaseitem WHERE monitor_databaseitem_time > (SELECT 
	max(monitor_databaseitem_time) - interval '12 hour' FROM monitor_databaseitem ) ORDER BY 2 ASC) AS b 
GROUP BY monitor_databaseitem_time ) AS b on a.row_number = b.row_number;


--make function for 12 hours data for tps qps connectnum dbsize indexsize
CREATE OR REPLACE FUNCTION pg_catalog.monitor_cluster_fouritem_func()
	RETURNS TABLE
	(
		timetmp timestamptz(0),
		tps  bigint,
		qps  bigint,
		connectnum bigint,
		dbsize numeric(18,2),
		indexsize numeric(18,2)
	)
	AS 
	$$
	SELECT time, tps, qps, connectnum, dbsize, indexsize
	FROM 
		adbmgr.monitor_cluster_fouritem_v
	$$
	LANGUAGE SQL
	IMMUTABLE
	RETURNS NULL ON NULL INPUT;	
		
--get first page first line values
CREATE VIEW adbmgr.monitor_cluster_firstline_v
AS
	SELECT * from 
	(SELECT monitor_databasetps_time::timestamptz(0) AS time, sum(monitor_databasetps_tps) AS tps , sum(monitor_databasetps_qps) AS qps, 
		sum(monitor_databaseitem_connectnum) AS connectnum , (sum(monitor_databaseitem_dbsize)/1024.0)::numeric(18,2) AS dbsize, (sum(monitor_databaseitem_indexsize)/1024.0)::numeric(18,2) AS indexsize, max(monitor_databasetps_runtime) as runtime
	FROM 
		(SELECT  tps.monitor_databasetps_time, tps.monitor_databasetps_tps,  tps.monitor_databasetps_qps, tps.monitor_databasetps_runtime,
					b.monitor_databaseitem_connectnum , b.monitor_databaseitem_dbsize, b.monitor_databaseitem_indexsize
				FROM (SELECT * ,(ROW_NUMBER()OVER(PARTITION BY monitor_databasetps_dbname ORDER BY 
									monitor_databasetps_time desc ))AS tc  
							FROM monitor_databasetps
							)AS tps
			JOIN 
			(SELECT monitor_databaseitem_dbname,
					monitor_databaseitem_connectnum,  monitor_databaseitem_dbsize, monitor_databaseitem_indexsize
			 FROM (SELECT * ,(ROW_NUMBER()OVER(PARTITION BY monitor_databaseitem_dbname ORDER BY 
									monitor_databaseitem_time desc ))AS tc  FROM monitor_databaseitem)AS item  WHERE item.tc =1
			) AS b
			on tps.monitor_databasetps_dbname = b.monitor_databaseitem_dbname 
			WHERE   tps.tc =1
		) AS c 
		GROUP BY  monitor_databasetps_time
	) AS d
	join 
	( SELECT (sum(md_total/1024.0/1024)/1024)::numeric(18,2) AS md_total FROM (SELECT  host_oid,md_timestamptz, md_total, (ROW_NUMBER()OVER(PARTITION BY host_oid  ORDER BY  md_timestamptz desc ))AS tc   from monitor_disk) AS d WHERE tc =1
	) AS e
	on 1=1;


--make function to get tps and qps for given dbname time and time interval
CREATE OR REPLACE FUNCTION pg_catalog.monitor_databasetps_func(in text, in timestamptz, in int)
		RETURNS TABLE
	(
		timetmp timestamptz(0),
		tps int,
		qps int
	)
	AS 
		$$
	SELECT monitor_databasetps_time::timestamptz(0) AS time,
				monitor_databasetps_tps   AS tps,
				monitor_databasetps_qps   AS qps
	FROM 
				monitor_databasetps
	WHERE monitor_databasetps_dbname = $1 and monitor_databasetps_time >= $2 and monitor_databasetps_time <= $2 + case $3
				when 0 then interval '1 hour'
				when 1 then interval '24 hour'
				end;
	$$
		LANGUAGE SQL
	IMMUTABLE
	RETURNS NULL ON NULL INPUT;	
--to show all database tps, qps, runtime at current_time
 CREATE VIEW adbmgr.monitor_all_dbname_tps_qps_runtime_v
 AS 
 SELECT distinct(monitor_databasetps_dbname) as databasename, monitor_databasetps_tps as tps, monitor_databasetps_qps as qps, monitor_databasetps_runtime as runtime FROM monitor_databasetps WHERE monitor_databasetps_time=(SELECT max(monitor_databasetps_time) FROM monitor_databasetps) ORDER BY 1 ASC;
--show cluster summary at current_time
CREATE VIEW adbmgr.monitor_cluster_summary_v
AS
SELECT (SUM(monitor_databaseitem_dbsize)/1024.0)::numeric(18,2) AS dbsize,
	CASE AVG(monitor_databaseitem_archivemode::int)
	 WHEN 0 THEN false
	 ELSE true
	 END AS archivemode,
	CASE AVG(monitor_databaseitem_autovacuum::int)
	 WHEN 0 THEN false
	 ELSE true
	 END AS autovacuum,
	AVG(monitor_databaseitem_heaphitrate)::numeric(18,2) AS heaphitrate,
	AVG(monitor_databaseitem_commitrate)::numeric(18,2) AS commitrate,
	AVG(monitor_databaseitem_dbage)::int AS dbage,
	SUM(monitor_databaseitem_connectnum) AS connectnum,
	AVG(monitor_databaseitem_standbydelay)::int AS standbydelay,
	SUM(monitor_databaseitem_locksnum) AS locksnum,
	SUM(monitor_databaseitem_longtransnum) AS longtransnum,
	SUM(monitor_databaseitem_idletransnum) AS idletransnum,
	SUM(monitor_databaseitem_preparenum) AS preparenum,
	SUM(monitor_databaseitem_unusedindexnum) AS unusedindexnum
from (SELECT * from monitor_databaseitem where monitor_databaseitem_time=(SELECT MAX(monitor_databaseitem_time) from monitor_databaseitem)) AS a;
--show database summary at current_time for given name
CREATE OR REPLACE FUNCTION pg_catalog.monitor_databasesummary_func(in name)
	RETURNS TABLE
	(
		dbsize 			numeric(18,2),
		archivemode		bool,
		autovacuum		bool,
		heaphitrate		numeric(18,2),
		commitrate		numeric(18,2),
		dbage			int,
		connectnum		int,
		standbydelay	int,
		locksnum		int,
		longtransnum	int,
		idletransnum	int,
		preparenum		int,
		unusedindexnum	int
	)
	AS 
		$$
	SELECT (monitor_databaseitem_dbsize/1024.0)::numeric(18,2) AS dbsize,
			 monitor_databaseitem_archivemode AS archivemode,
			 monitor_databaseitem_autovacuum  AS autovacuum,
			 monitor_databaseitem_heaphitrate::numeric(18,2) AS heaphitrate,
			 monitor_databaseitem_commitrate::numeric(18,2)  AS commitrate,
			 monitor_databaseitem_dbage       AS dbage,
			 monitor_databaseitem_connectnum  AS connectnum,
			 monitor_databaseitem_standbydelay AS standbydelay,
			 monitor_databaseitem_locksnum     AS locksnum,
			 monitor_databaseitem_longtransnum  AS longtransum,
			 monitor_databaseitem_idletransnum  AS idletransum,
			 monitor_databaseitem_preparenum    AS  preparenum,
			 monitor_databaseitem_unusedindexnum  AS unusedindexnum
	from (SELECT * from monitor_databaseitem where monitor_databaseitem_time=(SELECT 
		MAX(monitor_databaseitem_time) from monitor_databaseitem) and monitor_databaseitem_dbname = $1) AS a
	$$
		LANGUAGE SQL
	IMMUTABLE
	RETURNS NULL ON NULL INPUT;

CREATE OR REPLACE FUNCTION pg_catalog.monitor_slowlog_func(in name, in timestamptz, in timestamptz)
		RETURNS TABLE
		(
			query text,
			dbuser name,
			singletime float4,
			totalnum int,
			queryplan text
		)
	AS 
		$$
	SELECT slowlogquery AS query,
				slowloguser   AS dbuser,
				slowlogsingletime AS singletime,
				slowlogtotalnum   AS totalnum,
				slowlogqueryplan  AS queryplan
	FROM 
				monitor_slowlog
	WHERE slowlogdbname = $1 and slowlogtime >= $2 and slowlogtime < $3 order by slowlogtime desc;
	$$
		LANGUAGE SQL
	IMMUTABLE
	RETURNS NULL ON NULL INPUT;
	
CREATE OR REPLACE FUNCTION pg_catalog.monitor_slowlog_func_page(in name, in timestamptz, in timestamptz, in int, in int)
		RETURNS TABLE
		(
			query text,
			dbuser name,
			singletime float4,
			totalnum int,
			queryplan text
		)
	AS 
		$$
	SELECT slowlogquery AS query,
				slowloguser   AS dbuser,
				slowlogsingletime AS singletime,
				slowlogtotalnum   AS totalnum,
				slowlogqueryplan  AS queryplan
	FROM 
				monitor_slowlog
	WHERE slowlogdbname = $1 and slowlogtime >= $2 and slowlogtime < $3 order by slowlogtime desc limit $4 offset $5;
	$$
		LANGUAGE SQL
	IMMUTABLE
	RETURNS NULL ON NULL INPUT;
--for cluster warning, monitor_dbthreshold systbl, see "typedef enum DbthresholdObject" 
--  in monitor_dbthreshold.c
--heaphitrate
insert into pg_catalog.monitor_host_threshold values(11, 98, 95, 90);
insert into pg_catalog.monitor_host_threshold values(21, 98, 95, 90);
--commitrate
insert into pg_catalog.monitor_host_threshold values(12, 95, 90, 85);
insert into pg_catalog.monitor_host_threshold values(22, 95, 90, 85);
--standbydelay
insert into pg_catalog.monitor_host_threshold values(13, 200, 250, 300);
insert into pg_catalog.monitor_host_threshold values(23, 1000, 2000, 3000);
--locks num
insert into pg_catalog.monitor_host_threshold values(14, 40, 50, 60);
insert into pg_catalog.monitor_host_threshold values(24, 50, 80, 120);
--connect num
insert into pg_catalog.monitor_host_threshold values(15, 400, 500, 800);
insert into pg_catalog.monitor_host_threshold values(25, 500, 800, 1500);
--long transaction, idle transaction
insert into pg_catalog.monitor_host_threshold values(16, 25, 30, 50);
insert into pg_catalog.monitor_host_threshold values(26, 30, 60, 80);
--unused index num
insert into pg_catalog.monitor_host_threshold values(17, 0, 0, 0);
insert into pg_catalog.monitor_host_threshold values(27, 100, 150, 200);
--tps timeinterval
insert into pg_catalog.monitor_host_threshold values(31, 3, 0, 0);
--long transactions min time
insert into pg_catalog.monitor_host_threshold values(32, 100, 0, 0);
--slow query min time
insert into pg_catalog.monitor_host_threshold values(33, 2, 0, 0);
--get limit num slowlog from database cluster once time
insert into pg_catalog.monitor_host_threshold values(34, 5, 0, 0);

-- for ADB monitor the topology in home page : get datanode node topology
CREATE VIEW adbmgr.get_datanode_node_topology AS
    select '{'|| ARRAY_TO_STRING || '}' as datanode_result
    from (
    select ARRAY_TO_STRING(
                            array(
                                    select case f.nodetype
                                           when 'd' then '"master"'
                                           when 'b' then '"slave"'
                                           when 'e' then '"extra"'
                                           end 
                                           || ':' || '{' || '"node_name"' || ':' || '"' || f.nodename || '"' || ',' 
                                                         || '"node_port"' || ':' ||        f.nodeport        || ','
                                                         || '"node_ip"'   || ':' || '"' || f.hostaddr || '"' ||
                                                     '}'
                                    from(
                                            select n.nodename,n.oid,n.nodetype,n.nodeport,n.nodemasternameoid, h.hostaddr
                                            from mgr_node n, mgr_host h
                                            where n.nodemasternameoid = '0' and 
                                                  n.nodename = x.nodename  and 
                                                  n.nodetype = 'd' and
                                                  h.oid = n.nodehost and
                                                  n.nodeincluster = true and
                                                  n.nodeinited = true
                                            
                                            union all
                                            
                                            select t2.nodename,t2.oid,t2.nodetype,t2.nodeport,t2.nodemasternameoid,t2.hostaddr
                                            from (
                                                    select n.nodename,n.oid,n.nodetype,n.nodeport,n.nodemasternameoid,h.hostaddr
                                                    from mgr_node n,mgr_host h
                                                    where n.nodemasternameoid = '0' and 
                                                          n.nodename = x.nodename and 
                                                          n.nodetype = 'd' and
                                                          h.oid = n.nodehost and
                                                          n.nodeincluster = true and
                                                          n.nodeinited = true
                                                ) t1
                                                left join 
                                                (
                                                    select n.nodename,n.oid,n.nodetype,n.nodeport,n.nodemasternameoid,h.hostaddr
                                                    from mgr_node n,mgr_host h
                                                    where h.oid = n.nodehost and
                                                          n.nodeincluster = true and
                                                          n.nodeinited = true
                                                ) t2
                                                on t1.oid = t2.nodemasternameoid and t2.nodetype in ('b','e')
                                        ) as f
                                ), ','
                        ) from (select nodename from mgr_node where nodetype = 'd') x
        ) r;

-- for ADB monitor the topology in home page : get coordinator node topology 
CREATE VIEW adbmgr.get_coordinator_node_topology AS
    select n.nodename AS node_name,
        n.nodeport AS node_port,
        h.hostaddr AS node_ip
    from mgr_node n, mgr_host h
    where n.nodeincluster = true and
        n.nodeinited = true and
        n.nodehost = h.oid and
        n.nodetype = 'c';

-- for ADB monitor the topology in home page : get agtm node topology 
CREATE VIEW adbmgr.get_agtm_node_topology AS
    select '{'|| ARRAY_TO_STRING || '}' as agtm_result
    from(
    select ARRAY_TO_STRING(
                            array(
                                    select case f.nodetype
                                        when 'g' then '"master"'
                                        when 'p' then '"slave"'
                                        when 'x' then '"extra"'
                                        end 
                                        || ':' || '{' || '"node_name"' || ':' || '"' || f.nodename || '"' || ',' 
                                                      || '"node_port"' || ':' ||        f.nodeport        || ','
                                                      || '"node_ip"'   || ':' || '"' || f.hostaddr || '"' ||
                                                '}'
                                    from(
                                        select n.nodename,n.oid,n.nodetype,n.nodeport,n.nodemasternameoid, h.hostaddr
                                        from mgr_node n, mgr_host h
                                        where n.nodeincluster = true and
                                              n.nodeinited = true and
                                              n.nodehost = h.oid and
                                              n.nodetype in ('g', 'p', 'x')
                                        ) f
                                ) -- end array
                            , ','
                        ) -- end ARRAY_TO_STRING
        ) r;

-- insert default values into monitor_host_threthold.
insert into pg_catalog.monitor_host_threshold values (1, 90, 95, 99);
insert into pg_catalog.monitor_host_threshold values (2, 85, 90, 95);
insert into pg_catalog.monitor_host_threshold values (3, 80, 85, 90);
insert into pg_catalog.monitor_host_threshold values (4, 2000, 3000, 4000);
insert into pg_catalog.monitor_host_threshold values (5, 2000, 3000, 4000);
insert into pg_catalog.monitor_host_threshold values (6, 3000, 4000, 5000);

-- update waring value by type
create or replace function pg_catalog.update_warning_value(type int, value int)
returns  void
as $$
update pg_catalog.monitor_host_threshold
set mt_warning_threshold = $2
where mt_type = $1;
$$ language sql
VOLATILE
returns null on null input;

-- update critical value by type
create or replace function pg_catalog.update_critical_value(type int, value int)
returns  void
as $$
update pg_catalog.monitor_host_threshold
set mt_critical_threshold = $2
where mt_type = $1;
$$ language sql
VOLATILE
returns null on null input;

-- update emergency value by type
create or replace function pg_catalog.update_emergency_value(type int, value int)
returns  void
as $$
update pg_catalog.monitor_host_threshold
set mt_emergency_threshold = $2
where mt_type = $1;
$$ language sql
VOLATILE
returns null on null input;

--get the threshold for specific type
create or replace function pg_catalog.get_threshold_type(type int)
returns text
as $$
    select '{' || row_string || '}' as show_threshold
    from (
           select case $1
                  when 1 then '"cpu_usage"'
                  when 2 then '"mem_usage"'
                  when 3 then '"disk_usage"'
                  when 4 then '"sent_net"'
                  when 5 then '"recv_net"'
                  when 6 then '"IOPS"'
                  END
                  || ':' || row_to_json as row_string
           from (
           
                   select row_to_json(t)
                   from (
                           select 
                               mt_warning_threshold AS "warning",
                               mt_critical_threshold AS "critical",
                               mt_emergency_threshold AS "emergency"
                           from monitor_host_threshold
                           where mt_type = $1
                        ) t
               )y
         )s;
    $$
language sql
IMMUTABLE
RETURNS NULL ON NULL INPUT;

--get the threshlod for all type
CREATE VIEW adbmgr.get_threshold_all_type AS
select '{'|| ARRAY_TO_STRING || '}' as show_threshold
from(
        select ARRAY_TO_STRING(
        array(
                select  case f.type
                        when 1 then '"cpu_thresh"'
                        when 2 then '"mem_usage"'
                        when 3 then '"disk_usage"'
                        when 4 then '"sent_net"'
                        when 5 then '"recv_net"'
                        when 6 then '"IOPS"'
                        END
                        || ':' || '{' || '"warning"'   || ':' || f.warning   || ',' 
                                      || '"critical"'  || ':' || f.critical  || ','
                                      || '"emergency"' || ':' || f.emergency ||
                                   '}'
                from(
                        select mt_type AS type,
                               mt_warning_threshold AS "warning",
                               mt_critical_threshold AS "critical",
                               mt_emergency_threshold AS "emergency"
                        from monitor_host_threshold
                    )f
            ), ','
                            )
    ) r;

-- get all alarm info (host and DB)
create or replace FUNCTION pg_catalog.get_alarm_info_asc(starttime timestamp ,
                                                        endtime timestamp ,
                                                        search_text text default '',
                                                        alarm_level int default 0,
                                                        alarm_type int default 0,
                                                        alarm_status int default 0,
                                                        alarm_limit int default 20,
                                                        alarm_offset int default 0)
returns TABLE (
                alarm_level smallint,
                alarm_text text,
                alarm_type smallint,
                alarm_id oid,
                alarm_source text,
                alarm_time timestamptz,
                alarm_status smallint,
                alarm_resolve_timetz timestamptz,
                alarm_solution text)
as $$
select a.ma_alarm_level AS alarm_level,
       a.ma_alarm_text AS alarm_text,
       a.ma_alarm_type AS alarm_type,
       a.oid AS alarm_id,
       a.ma_alarm_source AS alarm_source,
       a.ma_alarm_timetz AS alarm_time,
       a.ma_alarm_status AS alarm_status,
       r.mr_resolve_timetz AS alarm_resolve_timetz,
       r.mr_solution AS alarm_solution
from monitor_alarm a left join monitor_resolve r on(a.oid = r.mr_alarm_oid)
where case $4 when 0 then 1::boolean else a.ma_alarm_level = $4 end and
      case $5 when 0 then 1::boolean else a.ma_alarm_type = $5 end and
      case $6 when 0 then 1::boolean else a.ma_alarm_status = $6 end and
      a.ma_alarm_text ~ $3 and
      a.ma_alarm_timetz between $1 and $2
    order by a.ma_alarm_timetz asc limit $7 offset $8

$$ LANGUAGE SQL
IMMUTABLE
RETURNS NULL ON NULL INPUT;

-- get all alarm info (host and DB)
create or replace FUNCTION pg_catalog.get_alarm_info_desc(starttime timestamp ,
                                                        endtime timestamp ,
                                                        search_text text default '',
                                                        alarm_level int default 0,
                                                        alarm_type int default 0,
                                                        alarm_status int default 0,
                                                        alarm_limit int default 20,
                                                        alarm_offset int default 0)
returns TABLE (
                alarm_level smallint,
                alarm_text text,
                alarm_type smallint,
                alarm_id oid,
                alarm_source text,
                alarm_time timestamptz,
                alarm_status smallint,
                alarm_resolve_timetz timestamptz,
                alarm_solution text)
as $$
select a.ma_alarm_level AS alarm_level,
       a.ma_alarm_text AS alarm_text,
       a.ma_alarm_type AS alarm_type,
       a.oid AS alarm_id,
       a.ma_alarm_source AS alarm_source,
       a.ma_alarm_timetz AS alarm_time,
       a.ma_alarm_status AS alarm_status,
       r.mr_resolve_timetz AS alarm_resolve_timetz,
       r.mr_solution AS alarm_solution
from monitor_alarm a left join monitor_resolve r on(a.oid = r.mr_alarm_oid)
where case $4 when 0 then 1::boolean else a.ma_alarm_level = $4 end and
      case $5 when 0 then 1::boolean else a.ma_alarm_type = $5 end and
      case $6 when 0 then 1::boolean else a.ma_alarm_status = $6 end and
      a.ma_alarm_text ~ $3 and
      a.ma_alarm_timetz between $1 and $2
    order by a.ma_alarm_timetz desc limit $7 offset $8

$$ LANGUAGE SQL
IMMUTABLE
RETURNS NULL ON NULL INPUT;

-- save resolve alarm log
create or replace function pg_catalog.resolve_alarm(alarm_id int, resolve_time timestamp, resolve_text text)
returns void as
$$
    insert into monitor_resolve (mr_alarm_oid, mr_resolve_timetz, mr_solution)
    values ($1, $2, $3);
    update monitor_alarm set ma_alarm_status = 2 where oideq(oid,$1)
$$
LANGUAGE SQL
VOLATILE
RETURNS NULL ON NULL INPUT;

--insert into monitor_user, as default value
insert into monitor_user values('数据库DBA', '系统用户', '2016-01-01','2050-01-01', '12345678901', 
'userdba@asiainfo.com', '亚信', '数据库', '数据库研发工程师', '21232f297a57a5a743894a0e4a801fc3','系统管理员');
--check user name/password
CREATE OR REPLACE  FUNCTION  pg_catalog.monitor_checkuser_func(in Name, in Name)
RETURNS oid AS $$
  select oid from pg_catalog.monitor_user where username=$1 and userpassword=$2;

$$  LANGUAGE sql IMMUTABLE STRICT;
	
--show user info
 create or replace function pg_catalog.monitor_getuserinfo_func(in int)
returns TABLE 
	(
		username			Name,				/*the user name*/
		userroletype		Name, 
		userstarttime		timestamptz,
		userendtime			timestamptz,
		usertel				Name,
		useremail			Name,
		usercompany			Name,
		userdepart			Name,
		usertitle			Name,
		userdesc			text
	)
as
$$
    select username, userroletype, userstarttime, userendtime, usertel, useremail
		, usercompany, userdepart, usertitle, userdesc from pg_catalog.monitor_user where oid=$1;
$$
LANGUAGE SQL
IMMUTABLE
RETURNS NULL ON NULL INPUT;

--update user info
create or replace function pg_catalog.monitor_updateuserinfo_func(in int, in Name, in Name, in Name, in Name, in Name, in text)
returns int
as
$$
    update pg_catalog.monitor_user set username=$2, usertel=$3, useremail=$4, usertitle=$5, usercompany=$6, userdesc=$7 where oid=$1 returning 0;
 
$$
LANGUAGE SQL
VOLATILE
RETURNS NULL ON NULL INPUT;

--check user oldpassword
create or replace function pg_catalog.monitor_checkuserpassword_func(in int, in Name)
returns bigint
as
$$
  select count(*) -1 from pg_catalog.monitor_user where oid=$1 and userpassword=$2;
$$
LANGUAGE SQL
VOLATILE
RETURNS NULL ON NULL INPUT;

--update user password
create or replace function  pg_catalog.monitor_updateuserpassword_func(in int, in Name)
returns int
as
$$
    update pg_catalog.monitor_user set userpassword=$2 where oid=$1  returning 0;
$$
LANGUAGE SQL
VOLATILE
RETURNS NULL ON NULL INPUT;


--insert data into mgr.parm

--insert gtm parameters
insert into adbmgr.parm values( 'g', 'nodename', 'one', 'string', 'Specifies the node name' );
insert into adbmgr.parm values( 'g', 'listen_addresses', '''*''', 'string', 'Listen addresses of this GTM.' );
insert into adbmgr.parm values( 'g', 'port', '6666', 'integer', 'Port number of this GTM' );
insert into adbmgr.parm values( 'g', 'startup', 'ACT', 'string', 'Start mode. ACT/STANDBY' );
insert into adbmgr.parm values( 'g', 'active_host', '''''', 'string', 'Listen address of active GTM.' );
insert into adbmgr.parm values( 'g', 'active_port', '''''', 'integer', 'Port number of active GTM.' );
insert into adbmgr.parm values( 'g', 'keepalives_idle', '0', 'integer', 'Keepalives_idle parameter.' );
insert into adbmgr.parm values( 'g', 'keepalives_interval', '0', 'integer', 'Keepalives_interval parameter.' );
insert into adbmgr.parm values( 'g', 'keepalives_count', '0', 'integer', 'Keepalives_count internal parameter.' );
insert into adbmgr.parm values( 'g', 'log_file', '''gtm.log''', 'string', 'Log file name' );
insert into adbmgr.parm values( 'g', 'log_min_messages', 'WARNING', 'enum', 'log_min_messages.  Default WARNING.' );
insert into adbmgr.parm values( 'g', 'synchronous_backup', 'off', 'bool', 'If backup to standby is synchronous' );
--insert gtm_proxy parameters
insert into adbmgr.parm values( 'p', 'nodename', 'one', 'string', 'Specifies the node name' );
insert into adbmgr.parm values( 'p', 'listen_addresses', '''*''', 'string', 'Listen addresses of this GTM.' );
insert into adbmgr.parm values( 'p', 'port', '6666', 'integer', 'Port number of this GTM' );
insert into adbmgr.parm values( 'p', 'worker_threads', '1', 'integer', 'Number of the worker thread of this' );
insert into adbmgr.parm values( 'p', 'gtm_host', '''localhost''', 'string', 'Listen address of the active GTM' );
insert into adbmgr.parm values( 'p', 'gtm_port', '6668', 'integer', 'Port number of the active GTM' );
insert into adbmgr.parm values( 'p', 'gtm_connect_retry_interval', '0', 'integer', 'How long (in secs) to wait until the next' );
insert into adbmgr.parm values( 'p', 'keepalives_idle', '0', 'integer', 'Keepalives_idle parameter' );
insert into adbmgr.parm values( 'p', 'keepalives_interval', '0', 'integer', 'Keepalives_interval parameter.' );
insert into adbmgr.parm values( 'p', 'keepalives_count', '0', 'integer', 'Keepalives_count internal parameter.' );
insert into adbmgr.parm values( 'p', 'log_file', '''gtm_proxy.log''', 'string', 'Log file name' );
insert into adbmgr.parm values( 'p', 'log_min_messages', 'WARNING', 'enum', 'log_min_messages.  Default WARNING.' );
--insert coordinator parameters
insert into adbmgr.parm values ( 'c', 'allow_system_table_mods', 'off', 'bool', 'Allows modifications of the structure of system tables.' );
insert into adbmgr.parm values ( 'c', 'application_name', 'psql', 'string', 'Sets the application name to be reported in statistics and logs.' );
insert into adbmgr.parm values ( 'c', 'archive_command', '(disabled)', 'string', 'Sets the shell command that will be called to archive a WAL file.' );
insert into adbmgr.parm values ( 'c', 'archive_mode', 'off', 'bool', 'Allows archiving of WAL files using archive_command.' );
insert into adbmgr.parm values ( 'c', 'archive_timeout', '0', 'integer', 'Forces a switch to the next xlog file if a new file has not been started within N seconds.' );
insert into adbmgr.parm values ( 'c', 'array_nulls', 'on', 'bool', 'Enable input of NULL elements in arrays.' );
insert into adbmgr.parm values ( 'c', 'authentication_timeout', '60', 'integer', 'Sets the maximum allowed time to complete client authentication.' );
insert into adbmgr.parm values ( 'c', 'autovacuum', 'on', 'bool', 'Starts the autovacuum subprocess.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_analyze_scale_factor', '0.1', 'real', 'Number of tuple inserts, updates, or deletes prior to analyze as a fraction of reltuples.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_analyze_threshold', '50', 'integer', 'Minimum number of tuple inserts, updates, or deletes prior to analyze.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_freeze_max_age', '200000000', 'integer', 'Age at which to autovacuum a table to prevent transaction ID wraparound.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_max_workers', '3', 'integer', 'Sets the maximum number of simultaneously running autovacuum worker processes.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_multixact_freeze_max_age', '400000000', 'integer', 'Multixact age at which to autovacuum a table to prevent multixact wraparound.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_naptime', '60', 'integer', 'Time to sleep between autovacuum runs.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_vacuum_cost_delay', '20', 'integer', 'Vacuum cost delay in milliseconds, for autovacuum.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_vacuum_cost_limit', '-1', 'integer', 'Vacuum cost amount available before napping, for autovacuum.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_vacuum_scale_factor', '0.2', 'real', 'Number of tuple updates or deletes prior to vacuum as a fraction of reltuples.' );
insert into adbmgr.parm values ( 'c', 'autovacuum_vacuum_threshold', '50', 'integer', 'Minimum number of tuple updates or deletes prior to vacuum.' );
insert into adbmgr.parm values ( 'c', 'backslash_quote', 'safe_encoding', 'enum', 'Sets whether "\\''" is allowed in string literals.' );
insert into adbmgr.parm values ( 'c', 'bgwriter_delay', '200', 'integer', 'Background writer sleep time between rounds.' );
insert into adbmgr.parm values ( 'c', 'bgwriter_lru_maxpages', '100', 'integer', 'Background writer maximum number of LRU pages to flush per round.' );
insert into adbmgr.parm values ( 'c', 'bgwriter_lru_multiplier', '2', 'real', 'Multiple of the average buffer usage to free per round.' );
--insert into adbmgr.parm values ( 'c', 'block_size', '8192', 'integer', 'Shows the size of a disk block.' );
insert into adbmgr.parm values ( 'c', 'bonjour', 'off', 'bool', 'Enables advertising the server via Bonjour.' );
insert into adbmgr.parm values ( 'c', 'bonjour_name', '', 'string', 'Sets the Bonjour service name.' );
insert into adbmgr.parm values ( 'c', 'bytea_output', 'hex', 'enum', 'Sets the output format for bytea.' );
insert into adbmgr.parm values ( 'c', 'check_function_bodies', 'on', 'bool', 'Check function bodies during CREATE FUNCTION.' );
insert into adbmgr.parm values ( 'c', 'checkpoint_completion_target', '0.5', 'real', 'Time spent flushing dirty buffers during checkpoint, as fraction of checkpoint interval.' );
insert into adbmgr.parm values ( 'c', 'checkpoint_segments', '3', 'integer', 'Sets the maximum distance in log segments between automatic WAL checkpoints.' );
insert into adbmgr.parm values ( 'c', 'checkpoint_timeout', '300', 'integer', 'Sets the maximum time between automatic WAL checkpoints.' );
insert into adbmgr.parm values ( 'c', 'checkpoint_warning', '30', 'integer', 'Enables warnings if checkpoint segments are filled more frequently than this.' );
insert into adbmgr.parm values ( 'c', 'client_encoding', 'UTF8', 'string', 'Sets the client''s character set encoding.' );
insert into adbmgr.parm values ( 'c', 'client_min_messages', 'notice', 'enum', 'Sets the message levels that are sent to the client.' );
insert into adbmgr.parm values ( 'c', 'commit_delay', '0', 'integer', 'Sets the delay in microseconds between transaction commit and flushing WAL to disk.' );
insert into adbmgr.parm values ( 'c', 'commit_siblings', '5', 'integer', 'Sets the minimum concurrent open transactions before performing commit_delay.' );
--insert into adbmgr.parm values ( 'c', 'config_file', '/home/wln/install_master/data/coordinator1/postgresql.conf', 'string', 'Sets the server''s main configuration file.' );
insert into adbmgr.parm values ( 'c', 'constraint_exclusion', 'partition', 'enum', 'Enables the planner to use constraints to optimize queries.' );
insert into adbmgr.parm values ( 'c', 'copy_batch_rows', '1000', 'integer', 'Set copy to table batch of max row' );
insert into adbmgr.parm values ( 'c', 'copy_batch_size', '64', 'integer', 'Set copy to table batch of max memory' );
insert into adbmgr.parm values ( 'c', 'cpu_index_tuple_cost', '0.005', 'real', 'Sets the planner''s estimate of the cost of processing each index entry during an index scan.' );
insert into adbmgr.parm values ( 'c', 'cpu_operator_cost', '0.0025', 'real', 'Sets the planner''s estimate of the cost of processing each operator or function call.' );
insert into adbmgr.parm values ( 'c', 'cpu_tuple_cost', '0.01', 'real', 'Sets the planner''s estimate of the cost of processing each tuple (row).' );
insert into adbmgr.parm values ( 'c', 'cursor_tuple_fraction', '0.1', 'real', 'Sets the planner''s estimate of the fraction of a cursor' );
--insert into adbmgr.parm values ( 'c', 'data_checksums', 'off', 'bool', 'Shows whether data checksums are turned on for this cluster.' );
--insert into adbmgr.parm values ( 'c', 'data_directory', '/home/wln/install_master/data/coordinator1', 'string', 'Sets the server''s data directory.' );
insert into adbmgr.parm values ( 'c', 'DateStyle', 'ISO, MDY', 'string', 'Sets the display format for date and time values.' );
insert into adbmgr.parm values ( 'c', 'db_user_namespace', 'off', 'bool', 'Enables per-database user names.' );
insert into adbmgr.parm values ( 'c', 'deadlock_timeout', '1000', 'integer', 'Sets the time to wait on a lock before checking for deadlock.' );
insert into adbmgr.parm values ( 'c', 'debug_assertions', 'on', 'bool', 'Turns on various assertion checks.' );
insert into adbmgr.parm values ( 'c', 'debug_pretty_print', 'on', 'bool', 'Indents parse and plan tree displays.' );
insert into adbmgr.parm values ( 'c', 'debug_print_grammar', 'off', 'bool', 'Logs each query''s grammar tree.' );
insert into adbmgr.parm values ( 'c', 'debug_print_parse', 'off', 'bool', 'Logs each query''s parse tree.' );
insert into adbmgr.parm values ( 'c', 'debug_print_plan', 'off', 'bool', 'Logs each query''s execution plan.' );
insert into adbmgr.parm values ( 'c', 'debug_print_rewritten', 'off', 'bool', 'Logs each query''s rewritten parse tree.' );
insert into adbmgr.parm values ( 'c', 'default_statistics_target', '100', 'integer', 'Sets the default statistics target.' );
insert into adbmgr.parm values ( 'c', 'default_tablespace', '', 'string', 'Sets the default tablespace to create tables and indexes in.' );
insert into adbmgr.parm values ( 'c', 'default_text_search_config', 'pg_catalog.english', 'string', 'Sets default text search configuration.' );
insert into adbmgr.parm values ( 'c', 'default_transaction_deferrable', 'off', 'bool', 'Sets the default deferrable status of new transactions.' );
insert into adbmgr.parm values ( 'c', 'default_transaction_isolation', 'read committed', 'enum', 'Sets the transaction isolation level of each new transaction.' );
insert into adbmgr.parm values ( 'c', 'default_transaction_read_only', 'off', 'bool', 'Sets the default read-only status of new transactions.' );
insert into adbmgr.parm values ( 'c', 'default_with_oids', 'off', 'bool', 'Create new tables with OIDs by default.' );
insert into adbmgr.parm values ( 'c', 'dynamic_library_path', '$libdir', 'string', 'Sets the path for dynamically loadable modules.' );
insert into adbmgr.parm values ( 'c', 'effective_cache_size', '16384', 'integer', 'Sets the planner''s assumption about the size of the disk cache.' );
insert into adbmgr.parm values ( 'c', 'effective_io_concurrency', '1', 'integer', 'Number of simultaneous requests that can be handled efficiently by the disk subsystem.' );
insert into adbmgr.parm values ( 'c', 'enable_bitmapscan', 'on', 'bool', 'Enables the planner''s use of bitmap-scan plans.' );
insert into adbmgr.parm values ( 'c', 'enable_fast_query_shipping', 'on', 'bool', 'Enables the planner''s use of fast query shipping to ship query directly to datanode.' );
insert into adbmgr.parm values ( 'c', 'enable_hashagg', 'on', 'bool', 'Enables the planner''s use of hashed aggregation plans.' );
insert into adbmgr.parm values ( 'c', 'enable_hashjoin', 'on', 'bool', 'Enables the planner''s use of hash join plans.' );
insert into adbmgr.parm values ( 'c', 'enable_indexonlyscan', 'on', 'bool', 'Enables the planner''s use of index-only-scan plans.' );
insert into adbmgr.parm values ( 'c', 'enable_indexscan', 'on', 'bool', 'Enables the planner''s use of index-scan plans.' );
insert into adbmgr.parm values ( 'c', 'enable_material', 'on', 'bool', 'Enables the planner''s use of materialization.' );
insert into adbmgr.parm values ( 'c', 'enable_mergejoin', 'on', 'bool', 'Enables the planner''s use of merge join plans.' );
insert into adbmgr.parm values ( 'c', 'enable_nestloop', 'on', 'bool', 'Enables the planner''s use of nested-loop join plans.' );
insert into adbmgr.parm values ( 'c', 'enable_remotegroup', 'on', 'bool', 'Enables the planner''s use of remote group plans.' );
insert into adbmgr.parm values ( 'c', 'enable_remotejoin', 'on', 'bool', 'Enables the planner''s use of remote join plans.' );
insert into adbmgr.parm values ( 'c', 'enable_remotelimit', 'on', 'bool', 'Enables the planner''s use of remote limit plans.' );
insert into adbmgr.parm values ( 'c', 'enable_remotesort', 'on', 'bool', 'Enables the planner''s use of remote sort plans.' );
insert into adbmgr.parm values ( 'c', 'enable_seqscan', 'on', 'bool', 'Enables the planner''s use of sequential-scan plans.' );
insert into adbmgr.parm values ( 'c', 'enable_sort', 'on', 'bool', 'Enables the planner''s use of explicit sort steps.' );
insert into adbmgr.parm values ( 'c', 'enable_tidscan', 'on', 'bool', 'Enables the planner''s use of TID scan plans.' );
insert into adbmgr.parm values ( 'c', 'enforce_two_phase_commit', 'on', 'bool', 'Enforce the use of two-phase commit on transactions thatmade use of temporary objects' );
insert into adbmgr.parm values ( 'c', 'escape_string_warning', 'on', 'bool', 'Warn about backslash escapes in ordinary string literals.' );
insert into adbmgr.parm values ( 'c', 'event_source', 'PostgreSQL', 'string', 'Sets the application name used to identify PostgreSQL messages in the event log.' );
insert into adbmgr.parm values ( 'c', 'exit_on_error', 'off', 'bool', 'Terminate session on any error.' );
insert into adbmgr.parm values ( 'c', 'external_pid_file', '', 'string', 'Writes the postmaster PID to the specified file.' );
insert into adbmgr.parm values ( 'c', 'extra_float_digits', '0', 'integer', 'Sets the number of digits displayed for floating-point values.' );
insert into adbmgr.parm values ( 'c', 'from_collapse_limit', '8', 'integer', 'Sets the FROM-list size beyond which subqueries are not collapsed.' );
insert into adbmgr.parm values ( 'c', 'fsync', 'on', 'bool', 'Forces synchronization of updates to disk.' );
insert into adbmgr.parm values ( 'c', 'full_page_writes', 'on', 'bool', 'Writes full pages to WAL when first modified after a checkpoint.' );
insert into adbmgr.parm values ( 'c', 'geqo', 'on', 'bool', 'Enables genetic query optimization.' );
insert into adbmgr.parm values ( 'c', 'geqo_effort', '5', 'integer', 'GEQO: effort is used to set the default for other GEQO parameters.' );
insert into adbmgr.parm values ( 'c', 'geqo_generations', '0', 'integer', 'GEQO: number of iterations of the algorithm.' );
insert into adbmgr.parm values ( 'c', 'geqo_pool_size', '0', 'integer', 'GEQO: number of individuals in the population.' );
insert into adbmgr.parm values ( 'c', 'geqo_seed', '0', 'real', 'GEQO: seed for random path selection.' );
insert into adbmgr.parm values ( 'c', 'geqo_selection_bias', '2', 'real', 'GEQO: selective pressure within the population.' );
insert into adbmgr.parm values ( 'c', 'geqo_threshold', '12', 'integer', 'Sets the threshold of FROM items beyond which GEQO is used.' );
insert into adbmgr.parm values ( 'c', 'gin_fuzzy_search_limit', '0', 'integer', 'Sets the maximum allowed result for exact search by GIN.' );
insert into adbmgr.parm values ( 'c', 'grammar', 'postgres', 'enum', 'Set SQL grammar' );
insert into adbmgr.parm values ( 'c', 'gtm_backup_barrier', 'off', 'bool', 'Enables coordinator to report barrier id to GTM for backup.' );
insert into adbmgr.parm values ( 'c', 'gtm_host', '127.0.0.1', 'string', 'Host name or address of GTM' );
insert into adbmgr.parm values ( 'c', 'gtm_port', '5706', 'integer', 'Port of GTM.' );
--insert into adbmgr.parm values ( 'c', 'hba_file', '/home/wln/install_master/data/coordinator1/pg_hba.conf', 'string', 'Sets the server''s "hba" configuration file.' );
insert into adbmgr.parm values ( 'c', 'hot_standby', 'off', 'bool', 'Allows connections and queries during recovery.' );
insert into adbmgr.parm values ( 'c', 'hot_standby_feedback', 'off', 'bool', 'Allows feedback from a hot standby to the primary that will avoid query conflicts.' );
--insert into adbmgr.parm values ( 'c', 'ident_file', '/home/wln/install_master/data/coordinator1/pg_ident.conf', 'string', 'Sets the server''s "ident" configuration file.' );
insert into adbmgr.parm values ( 'c', 'ignore_checksum_failure', 'off', 'bool', 'Continues processing after a checksum failure.' );
insert into adbmgr.parm values ( 'c', 'ignore_system_indexes', 'off', 'bool', 'Disables reading from system indexes.' );
--insert into adbmgr.parm values ( 'c', 'integer_datetimes', 'on', 'bool', 'Datetimes are integer based.' );
insert into adbmgr.parm values ( 'c', 'IntervalStyle', 'postgres', 'enum', 'Sets the display format for interval values.' );
insert into adbmgr.parm values ( 'c', 'join_collapse_limit', '8', 'integer', 'Sets the FROM-list size beyond which JOIN constructs are not flattened.' );
insert into adbmgr.parm values ( 'c', 'krb_caseins_users', 'off', 'bool', 'Sets whether Kerberos and GSSAPI user names should be treated as case-insensitive.' );
insert into adbmgr.parm values ( 'c', 'krb_server_keyfile', '', 'string', 'Sets the location of the Kerberos server key file.' );
insert into adbmgr.parm values ( 'c', 'krb_srvname', 'postgres', 'string', 'Sets the name of the Kerberos service.' );
--insert into adbmgr.parm values ( 'c', 'lc_collate', 'en_US.UTF-8', 'string', 'Shows the collation order locale.' );
--insert into adbmgr.parm values ( 'c', 'lc_ctype', 'en_US.UTF-8', 'string', 'Shows the character classification and case conversion locale.' );
insert into adbmgr.parm values ( 'c', 'lc_messages', 'en_US.UTF-8', 'string', 'Sets the language in which messages are displayed.' );
insert into adbmgr.parm values ( 'c', 'lc_monetary', 'en_US.UTF-8', 'string', 'Sets the locale for formatting monetary amounts.' );
insert into adbmgr.parm values ( 'c', 'lc_numeric', 'en_US.UTF-8', 'string', 'Sets the locale for formatting numbers.' );
insert into adbmgr.parm values ( 'c', 'lc_time', 'en_US.UTF-8', 'string', 'Sets the locale for formatting date and time values.' );
insert into adbmgr.parm values ( 'c', 'listen_addresses', 'localhost', 'string', 'Sets the host name or IP address(es) to listen to.' );
insert into adbmgr.parm values ( 'c', 'local_preload_libraries', '', 'string', 'Lists shared libraries to preload into each backend.' );
insert into adbmgr.parm values ( 'c', 'lock_timeout', '0', 'integer', 'Sets the maximum allowed duration of any wait for a lock.' );
insert into adbmgr.parm values ( 'c', 'lo_compat_privileges', 'off', 'bool', 'Enables backward compatibility mode for privilege checks on large objects.' );
insert into adbmgr.parm values ( 'c', 'log_autovacuum_min_duration', '-1', 'integer', 'Sets the minimum execution time above which autovacuum actions will be logged.' );
insert into adbmgr.parm values ( 'c', 'log_checkpoints', 'off', 'bool', 'Logs each checkpoint.' );
insert into adbmgr.parm values ( 'c', 'log_connections', 'off', 'bool', 'Logs each successful connection.' );
insert into adbmgr.parm values ( 'c', 'log_destination', 'stderr', 'string', 'Sets the destination for server log output.' );
insert into adbmgr.parm values ( 'c', 'log_directory', 'pg_log', 'string', 'Sets the destination directory for log files.' );
insert into adbmgr.parm values ( 'c', 'log_disconnections', 'off', 'bool', 'Logs end of a session, including duration.' );
insert into adbmgr.parm values ( 'c', 'log_duration', 'off', 'bool', 'Logs the duration of each completed SQL statement.' );
insert into adbmgr.parm values ( 'c', 'log_error_verbosity', 'default', 'enum', 'Sets the verbosity of logged messages.' );
insert into adbmgr.parm values ( 'c', 'log_executor_stats', 'off', 'bool', 'Writes executor performance statistics to the server log.' );
insert into adbmgr.parm values ( 'c', 'log_file_mode', '0600', 'integer', 'Sets the file permissions for log files.' );
insert into adbmgr.parm values ( 'c', 'log_filename', 'postgresql-%Y-%m-%d_%H%M%S.log', 'string', 'Sets the file name pattern for log files.' );
insert into adbmgr.parm values ( 'c', 'logging_collector', 'off', 'bool', 'Start a subprocess to capture stderr output and/or csvlogs into log files.' );
insert into adbmgr.parm values ( 'c', 'log_hostname', 'off', 'bool', 'Logs the host name in the connection logs.' );
insert into adbmgr.parm values ( 'c', 'log_line_prefix', '', 'string', 'Controls information prefixed to each log line.' );
insert into adbmgr.parm values ( 'c', 'log_lock_waits', 'off', 'bool', 'Logs long lock waits.' );
insert into adbmgr.parm values ( 'c', 'log_min_duration_statement', '-1', 'integer', 'Sets the minimum execution time above which statements will be logged.' );
insert into adbmgr.parm values ( 'c', 'log_min_error_statement', 'error', 'enum', 'Causes all statements generating error at or above this level to be logged.' );
insert into adbmgr.parm values ( 'c', 'log_min_messages', 'warning', 'enum', 'Sets the message levels that are logged.' );
insert into adbmgr.parm values ( 'c', 'log_parser_stats', 'off', 'bool', 'Writes parser performance statistics to the server log.' );
insert into adbmgr.parm values ( 'c', 'log_planner_stats', 'off', 'bool', 'Writes planner performance statistics to the server log.' );
insert into adbmgr.parm values ( 'c', 'log_rotation_age', '1440', 'integer', 'Automatic log file rotation will occur after N minutes.' );
insert into adbmgr.parm values ( 'c', 'log_rotation_size', '10240', 'integer', 'Automatic log file rotation will occur after N kilobytes.' );
insert into adbmgr.parm values ( 'c', 'log_statement', 'none', 'enum', 'Sets the type of statements logged.' );
insert into adbmgr.parm values ( 'c', 'log_statement_stats', 'off', 'bool', 'Writes cumulative performance statistics to the server log.' );
insert into adbmgr.parm values ( 'c', 'log_temp_files', '-1', 'integer', 'Log the use of temporary files larger than this number of kilobytes.' );
insert into adbmgr.parm values ( 'c', 'log_timezone', 'Hongkong', 'string', 'Sets the time zone to use in log messages.' );
insert into adbmgr.parm values ( 'c', 'log_truncate_on_rotation', 'off', 'bool', 'Truncate existing log files of same name during log rotation.' );
insert into adbmgr.parm values ( 'c', 'maintenance_work_mem', '16384', 'integer', 'Sets the maximum memory to be used for maintenance operations.' );
insert into adbmgr.parm values ( 'c', 'max_connections', '100', 'integer', 'Sets the maximum number of concurrent connections.' );
insert into adbmgr.parm values ( 'c', 'max_coordinators', '16', 'integer', 'Maximum number of Coordinators in the cluster.' );
insert into adbmgr.parm values ( 'c', 'max_datanodes', '16', 'integer', 'Maximum number of Datanodes in the cluster.' );
insert into adbmgr.parm values ( 'c', 'max_files_per_process', '1000', 'integer', 'Sets the maximum number of simultaneously open files for each server process.' );
--insert into adbmgr.parm values ( 'c', 'max_function_args', '100', 'integer', 'Shows the maximum number of function arguments.' );
--insert into adbmgr.parm values ( 'c', 'max_identifier_length', '63', 'integer', 'Shows the maximum identifier length.' );
--insert into adbmgr.parm values ( 'c', 'max_index_keys', '32', 'integer', 'Shows the maximum number of index keys.' );
insert into adbmgr.parm values ( 'c', 'max_locks_per_transaction', '64', 'integer', 'Sets the maximum number of locks per transaction.' );
insert into adbmgr.parm values ( 'c', 'max_pool_size', '100', 'integer', 'Max pool size.' );
insert into adbmgr.parm values ( 'c', 'max_pred_locks_per_transaction', '64', 'integer', 'Sets the maximum number of predicate locks per transaction.' );
insert into adbmgr.parm values ( 'c', 'max_prepared_transactions', '10', 'integer', 'Sets the maximum number of simultaneously prepared transactions.' );
insert into adbmgr.parm values ( 'c', 'max_stack_depth', '2048', 'integer', 'Sets the maximum stack depth, in kilobytes.' );
insert into adbmgr.parm values ( 'c', 'max_standby_archive_delay', '30000', 'integer', 'Sets the maximum delay before canceling queries when a hot standby server is processing archived WAL data.' );
insert into adbmgr.parm values ( 'c', 'max_standby_streaming_delay', '30000', 'integer', 'Sets the maximum delay before canceling queries when a hot standby server is processing streamed WAL data.' );
insert into adbmgr.parm values ( 'c', 'max_wal_senders', '0', 'integer', 'Sets the maximum number of simultaneously running WAL sender processes.' );
insert into adbmgr.parm values ( 'c', 'min_pool_size', '1', 'integer', 'Initial pool size.' );
insert into adbmgr.parm values ( 'c', 'nls_date_format', 'YYYY-MM-DD', 'string', 'Emulate oracle''s date output behaviour.' );
insert into adbmgr.parm values ( 'c', 'nls_timestamp_format', 'YYYY-MM-DD HH24:MI:SS.US', 'string', 'Emulate oracle''s timestamp without time zone output behaviour.' );
insert into adbmgr.parm values ( 'c', 'nls_timestamp_tz_format', 'YYYY-MM-DD HH24:MI:SS.US TZ', 'string', 'Emulate oracle''s timestamp with time zone output behaviour.' );
insert into adbmgr.parm values ( 'c', 'password_encryption', 'on', 'bool', 'Encrypt passwords.' );
insert into adbmgr.parm values ( 'c', 'persistent_datanode_connections', 'off', 'bool', 'Session never releases acquired connections.' );
insert into adbmgr.parm values ( 'c', 'pgxcnode_cancel_delay', '10', 'integer', 'Cancel deay dulation at the coordinator.' );
insert into adbmgr.parm values ( 'c', 'pgxc_node_name', 'coordinator1', 'string', 'The Coordinator or Datanode name.' );
insert into adbmgr.parm values ( 'c', 'port', '5700', 'integer', 'Sets the TCP port the server listens on.' );
insert into adbmgr.parm values ( 'c', 'post_auth_delay', '0', 'integer', 'Waits N seconds on connection startup after authentication.' );
insert into adbmgr.parm values ( 'c', 'pre_auth_delay', '0', 'integer', 'Waits N seconds on connection startup before authentication.' );
insert into adbmgr.parm values ( 'c', 'quote_all_identifiers', 'off', 'bool', 'When generating SQL fragments, quote all identifiers.' );
insert into adbmgr.parm values ( 'c', 'random_page_cost', '4', 'real', 'Sets the planner''s estimate of the cost of a nonsequentially fetched disk page.' );
insert into adbmgr.parm values ( 'c', 'remotetype', 'application', 'enum', 'Sets the type of Postgres-XC remote connection' );
insert into adbmgr.parm values ( 'c', 'require_replicated_table_pkey', 'on', 'bool', 'When set, non-FQS UPDATEs & DELETEs to replicated tables without primary key or a unique key are prohibited' );
insert into adbmgr.parm values ( 'c', 'restart_after_crash', 'on', 'bool', 'Reinitialize server after backend crash.' );
insert into adbmgr.parm values ( 'c', 'search_path', '"$user",public', 'string', 'Sets the schema search order for names that are not schema-qualified.' );
--insert into adbmgr.parm values ( 'c', 'segment_size', '131072', 'integer', 'Shows the number of pages per disk file.' );
insert into adbmgr.parm values ( 'c', 'seq_page_cost', '1', 'real', 'Sets the planner''s estimate of the cost of a sequentially fetched disk page.' );
--insert into adbmgr.parm values ( 'c', 'server_encoding', 'UTF8', 'string', 'Sets the server (database) character set encoding.' );
--insert into adbmgr.parm values ( 'c', 'server_version', '9.3.9', 'string', 'Shows the server version.' );
--insert into adbmgr.parm values ( 'c', 'server_version_num', '90309', 'integer', 'Shows the server version as an integer.' );
insert into adbmgr.parm values ( 'c', 'session_replication_role', 'origin', 'enum', 'Sets the session''s behavior for triggers and rewrite rules.' );
insert into adbmgr.parm values ( 'c', 'shared_buffers', '16384', 'integer', 'Sets the number of shared memory buffers used by the server.' );
insert into adbmgr.parm values ( 'c', 'shared_preload_libraries', '', 'string', 'Lists shared libraries to preload into server.' );
insert into adbmgr.parm values ( 'c', 'sql_inheritance', 'on', 'bool', 'Causes subtables to be included by default in various commands.' );
insert into adbmgr.parm values ( 'c', 'ssl', 'off', 'bool', 'Enables SSL connections.' );
insert into adbmgr.parm values ( 'c', 'ssl_ca_file', '', 'string', 'Location of the SSL certificate authority file.' );
insert into adbmgr.parm values ( 'c', 'ssl_cert_file', 'server.crt', 'string', 'Location of the SSL server certificate file.' );
insert into adbmgr.parm values ( 'c', 'ssl_ciphers', 'none', 'string', 'Sets the list of allowed SSL ciphers.' );
insert into adbmgr.parm values ( 'c', 'ssl_crl_file', '', 'string', 'Location of the SSL certificate revocation list file.' );
insert into adbmgr.parm values ( 'c', 'ssl_key_file', 'server.key', 'string', 'Location of the SSL server private key file.' );
insert into adbmgr.parm values ( 'c', 'ssl_renegotiation_limit', '524288', 'integer', 'Set the amount of traffic to send and receive before renegotiating the encryption keys.' );
insert into adbmgr.parm values ( 'c', 'standard_conforming_strings', 'on', 'bool', 'Causes ''...' );
insert into adbmgr.parm values ( 'c', 'statement_timeout', '0', 'integer', 'Sets the maximum allowed duration of any statement.' );
insert into adbmgr.parm values ( 'c', 'stats_temp_directory', 'pg_stat_tmp', 'string', 'Writes temporary statistics files to the specified directory.' );
insert into adbmgr.parm values ( 'c', 'superuser_reserved_connections', '3', 'integer', 'Sets the number of connection slots reserved for superusers.' );
insert into adbmgr.parm values ( 'c', 'synchronize_seqscans', 'on', 'bool', 'Enable synchronized sequential scans.' );
insert into adbmgr.parm values ( 'c', 'synchronous_commit', 'on', 'enum', 'Sets the current transaction''s synchronization level.' );
insert into adbmgr.parm values ( 'c', 'synchronous_standby_names', '', 'string', 'List of names of potential synchronous standbys.' );
insert into adbmgr.parm values ( 'c', 'syslog_facility', 'local0', 'enum', 'Sets the syslog "facility" to be used when syslog enabled.' );
insert into adbmgr.parm values ( 'c', 'syslog_ident', 'postgres', 'string', 'Sets the program name used to identify PostgreSQL messages in syslog.' );
insert into adbmgr.parm values ( 'c', 'tcp_keepalives_count', '0', 'integer', 'Maximum number of TCP keepalive retransmits.' );
insert into adbmgr.parm values ( 'c', 'tcp_keepalives_idle', '0', 'integer', 'Time between issuing TCP keepalives.' );
insert into adbmgr.parm values ( 'c', 'tcp_keepalives_interval', '0', 'integer', 'Time between TCP keepalive retransmits.' );
insert into adbmgr.parm values ( 'c', 'temp_buffers', '1024', 'integer', 'Sets the maximum number of temporary buffers used by each session.' );
insert into adbmgr.parm values ( 'c', 'temp_file_limit', '-1', 'integer', 'Limits the total size of all temporary files used by each session.' );
insert into adbmgr.parm values ( 'c', 'temp_tablespaces', '', 'string', 'Sets the tablespace(s) to use for temporary tables and sort files.' );
insert into adbmgr.parm values ( 'c', 'TimeZone', 'Hongkong', 'string', 'Sets the time zone for displaying and interpreting time stamps.' );
insert into adbmgr.parm values ( 'c', 'timezone_abbreviations', 'Default', 'string', 'Selects a file of time zone abbreviations.' );
insert into adbmgr.parm values ( 'c', 'trace_notify', 'off', 'bool', 'Generates debugging output for LISTEN and NOTIFY.' );
insert into adbmgr.parm values ( 'c', 'trace_recovery_messages', 'log', 'enum', 'Enables logging of recovery-related debugging information.' );
insert into adbmgr.parm values ( 'c', 'trace_sort', 'off', 'bool', 'Emit information about resource usage in sorting.' );
insert into adbmgr.parm values ( 'c', 'track_activities', 'on', 'bool', 'Collects information about executing commands.' );
insert into adbmgr.parm values ( 'c', 'track_activity_query_size', '1024', 'integer', 'Sets the size reserved for pg_stat_activity.query, in bytes.' );
insert into adbmgr.parm values ( 'c', 'track_counts', 'on', 'bool', 'Collects statistics on database activity.' );
insert into adbmgr.parm values ( 'c', 'track_functions', 'none', 'enum', 'Collects function-level statistics on database activity.' );
insert into adbmgr.parm values ( 'c', 'track_io_timing', 'off', 'bool', 'Collects timing statistics for database I/O activity.' );
insert into adbmgr.parm values ( 'c', 'transaction_deferrable', 'off', 'bool', 'Whether to defer a read-only serializable transaction until it can be executed with no possible serialization failures.' );
insert into adbmgr.parm values ( 'c', 'transaction_isolation', 'read committed', 'string', 'Sets the current transaction''s isolation level.' );
insert into adbmgr.parm values ( 'c', 'transaction_read_only', 'off', 'bool', 'Sets the current transaction''s read-only status.' );
insert into adbmgr.parm values ( 'c', 'transform_null_equals', 'off', 'bool', 'Treats "expr=NULL" as "expr IS NULL".' );
insert into adbmgr.parm values ( 'c', 'unix_socket_directories', '/tmp', 'string', 'Sets the directories where Unix-domain sockets will be created.' );
insert into adbmgr.parm values ( 'c', 'unix_socket_group', '', 'string', 'Sets the owning group of the Unix-domain socket.' );
insert into adbmgr.parm values ( 'c', 'unix_socket_permissions', '0777', 'integer', 'Sets the access permissions of the Unix-domain socket.' );
insert into adbmgr.parm values ( 'c', 'update_process_title', 'on', 'bool', 'Updates the process title to show the active SQL command.' );
insert into adbmgr.parm values ( 'c', 'vacuum_cost_delay', '0', 'integer', 'Vacuum cost delay in milliseconds.' );
insert into adbmgr.parm values ( 'c', 'vacuum_cost_limit', '200', 'integer', 'Vacuum cost amount available before napping.' );
insert into adbmgr.parm values ( 'c', 'vacuum_cost_page_dirty', '20', 'integer', 'Vacuum cost for a page dirtied by vacuum.' );
insert into adbmgr.parm values ( 'c', 'vacuum_cost_page_hit', '1', 'integer', 'Vacuum cost for a page found in the buffer cache.' );
insert into adbmgr.parm values ( 'c', 'vacuum_cost_page_miss', '10', 'integer', 'Vacuum cost for a page not found in the buffer cache.' );
insert into adbmgr.parm values ( 'c', 'vacuum_defer_cleanup_age', '0', 'integer', 'Number of transactions by which VACUUM and HOT cleanup should be deferred, if any.' );
insert into adbmgr.parm values ( 'c', 'vacuum_freeze_min_age', '50000000', 'integer', 'Minimum age at which VACUUM should freeze a table row.' );
insert into adbmgr.parm values ( 'c', 'vacuum_freeze_table_age', '150000000', 'integer', 'Age at which VACUUM should scan whole table to freeze tuples.' );
insert into adbmgr.parm values ( 'c', 'vacuum_multixact_freeze_min_age', '5000000', 'integer', 'Minimum age at which VACUUM should freeze a MultiXactId in a table row.' );
insert into adbmgr.parm values ( 'c', 'vacuum_multixact_freeze_table_age', '150000000', 'integer', 'Multixact age at which VACUUM should scan whole table to freeze tuples.' );
--insert into adbmgr.parm values ( 'c', 'wal_block_size', '8192', 'integer', 'Shows the block size in the write ahead log.' );
insert into adbmgr.parm values ( 'c', 'wal_buffers', '512', 'integer', 'Sets the number of disk-page buffers in shared memory for WAL.' );
insert into adbmgr.parm values ( 'c', 'wal_keep_segments', '0', 'integer', 'Sets the number of WAL files held for standby servers.' );
insert into adbmgr.parm values ( 'c', 'wal_level', 'minimal', 'enum', 'Set the level of information written to the WAL.' );
insert into adbmgr.parm values ( 'c', 'wal_receiver_status_interval', '10', 'integer', 'Sets the maximum interval between WAL receiver status reports to the primary.' );
insert into adbmgr.parm values ( 'c', 'wal_receiver_timeout', '60000', 'integer', 'Sets the maximum wait time to receive data from the primary.' );
--insert into adbmgr.parm values ( 'c', 'wal_segment_size', '2048', 'integer', 'Shows the number of pages per write ahead log segment.' );
insert into adbmgr.parm values ( 'c', 'wal_sender_timeout', '60000', 'integer', 'Sets the maximum time to wait for WAL replication.' );
insert into adbmgr.parm values ( 'c', 'wal_sync_method', 'fdatasync', 'enum', 'Selects the method used for forcing WAL updates to disk.' );
insert into adbmgr.parm values ( 'c', 'wal_writer_delay', '200', 'integer', 'WAL writer sleep time between WAL flushes.' );
insert into adbmgr.parm values ( 'c', 'work_mem', '1024', 'integer', 'Sets the maximum memory to be used for query workspaces.' );
insert into adbmgr.parm values ( 'c', 'xc_enable_node_tcp_log', 'off', 'bool', 'Save node TCP data to log file' );
insert into adbmgr.parm values ( 'c', 'xc_gtm_commit_sync_test', 'off', 'bool', 'Prints additional status info on GTM commit syncronization.' );
--insert into adbmgr.parm values ( 'c', 'xc_gtm_sync_timeout', '2000', 'integer', 'Timeout to synchronize commit report to GTM' );
--insert into adbmgr.parm values ( 'c', 'xc_maintenance_mode', 'off', 'bool', 'Turn on XC maintenance mode.' );
insert into adbmgr.parm values ( 'c', 'xmlbinary', 'base64', 'enum', 'Sets how binary values are to be encoded in XML.' );
insert into adbmgr.parm values ( 'c', 'xmloption', 'content', 'enum', 'Sets whether XML data in implicit parsing and serialization operations is to be considered as documents or content fragments.' );
insert into adbmgr.parm values ( 'c', 'zero_damaged_pages', 'off', 'bool', 'Continues processing past damaged page headers.' );

