#ifndef SNAP_RECEIVER_H_
#define SNAP_RECEIVER_H_

#include "storage/lockdefs.h"
#include "storage/sinval.h"
#include "utils/snapshot.h"
#include "lib/ilist.h"
#include "replication/snapcommon.h"

extern int snap_receiver_sxmin_time;
/* prototypes for functions in snapreceiver.c */
extern void SnapReceiverMain(void) pg_attribute_noreturn();

extern Size SnapRcvShmemSize(void);
extern void SnapRcvShmemInit(void);
extern void ShutdownSnapRcv(void);
extern bool SnapRcvStreaming(void);
extern bool SnapRcvRunning(void);

extern Snapshot SnapRcvGetSnapshot(Snapshot snap, TransactionId last_mxid, TransactionId last_finish_xid, bool isCatalog);
extern bool SnapRcvWaitTopTransactionEnd(TransactionId txid, TimestampTz end);

struct PGPROC;
extern void SnapRcvTransferLock(void **param, TransactionId xid, struct PGPROC *from);
extern TransactionId SnapRcvGetGlobalXmin(void);
extern TransactionId SnapRcvGetLastDdlFinishXid(void);
#endif							/* SNAP_RECEIVER_H_ */