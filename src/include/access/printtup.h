/*-------------------------------------------------------------------------
 *
 * printtup.h
 *
 *
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/printtup.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PRINTTUP_H
#define PRINTTUP_H

#include "utils/portal.h"

extern DestReceiver *printtup_create_DR(CommandDest dest);

extern void SetRemoteDestReceiverParams(DestReceiver *self, Portal portal);

extern void SendRowDescriptionMessage(StringInfo buf,
									  TupleDesc typeinfo, List *targetlist, int16 *formats);
#ifdef ADB_GRAM_ORA
extern void SendRowDescriptionMessageUpperAttributeName(StringInfo buf,
						  TupleDesc typeinfo, List *targetlist, int16 *formats,
						  const char *source_cmd, bool upper_target);
#endif /* ADB_GRAM_ORA */

#ifdef ADB
extern void StartupRemoteDestReceiver(DestReceiver *self, TupleDesc typeinfo,
						  int16 *formats);
#endif

extern void debugStartup(DestReceiver *self, int operation,
						 TupleDesc typeinfo);
extern bool debugtup(TupleTableSlot *slot, DestReceiver *self);

/* XXX these are really in executor/spi.c */
extern void spi_dest_startup(DestReceiver *self, int operation,
							 TupleDesc typeinfo);
extern bool spi_printtup(TupleTableSlot *slot, DestReceiver *self);

#endif							/* PRINTTUP_H */
