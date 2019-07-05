/*--------------------------------------------------------------------------
 *
 * Copyright (c) 2018-2019, Asiainfo Database Innovation Lab
 *
 * -------------------------------------------------------------------------
 */
#ifndef ADB_DOCTOR_SQL_H
#define ADB_DOCTOR_SQL_H

#include "utils/memutils.h"
#include "adb_doctor_conf.h"
#include "adb_doctor_data.h"

#ifndef ADBMGR_DBNAME
#define ADBMGR_DBNAME "postgres"
#endif

#define ADB_DOCTOR_SCHEMA "adb_doctor"

/* SPI functions */
extern char *SPI_selectAdbDoctConfByKey(char *key);
extern void SPI_selectAdbDoctorConfAll(AdbDoctorConf **confP, MemoryContext ctx);
extern void SPI_updateAdbDoctorConf(char *key, char *value);
extern AdbDoctorList *SPI_selectMgrNodeForMonitor(MemoryContext ctx);
extern AdbDoctorList *SPI_selectMgrNodeForSwitcher(MemoryContext ctx);
extern AdbDoctorHostData *SPI_selectMgrHostForMonitor(MemoryContext ctx);
extern AdbMgrHostWrapper *SPI_selectMgrHostByOid(MemoryContext ctx, Oid oid);
extern AdbDoctorList *SPI_selectMgrNode(MemoryContext ctx, char *sql);
extern AdbDoctorList *SPI_selectMgrHost(MemoryContext ctx, char *sql);

#endif