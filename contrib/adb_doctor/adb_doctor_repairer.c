/*--------------------------------------------------------------------------
 *
 * Copyright (c) 2018-2019, Asiainfo Database Innovation Lab
 *
 * -------------------------------------------------------------------------
 */
#include <math.h>
#include "postgres.h"
#include "miscadmin.h"
#include "access/htup_details.h"
#include "storage/ipc.h"
#include "storage/procarray.h"
#include "storage/spin.h"
#include "executor/spi.h"
#include "parser/parser.h"
#include "utils/resowner.h"
#include "utils/builtins.h"
#include "utils/ps_status.h"
#include "utils/memutils.h"
#include "utils/typcache.h"
#include "../../src/interfaces/libpq/libpq-fe.h"
#include "../../src/interfaces/libpq/libpq-int.h"
#include "mgr/mgr_agent.h"
#include "mgr/mgr_msg_type.h"
#include "mgr/mgr_cmds.h"
#include "mgr/mgr_helper.h"
#include "mgr/mgr_switcher.h"
#include "adb_doctor.h"

typedef struct RepairerConfiguration
{
	long repairIntervalMs;
} RepairerConfiguration;

static void repairerMainLoop(dlist_head *mgrNodes);
static void checkAndRepairNodes(dlist_head *faultNodes);
static void repairSpecificMgrNode(dlist_head *mgrNodes, char nodetype);
static bool cleanFaultNodesOnCoordinators(dlist_head *faultNodes);
static bool repairCoordinatorNode(MgrNodeWrapper *faultCoordinator);
static bool repairSlaveNode(MgrNodeWrapper *faultSlaveNode);
// static void addDataNodeMasterToCoordinator(MgrNodeWrapper *coordinator,
// 										   PGconn *activeConn,
// 										   bool localExecute,
// 										   char *executeOnNodeName,
// 										   MemoryContext spiContext);
static bool checkGetMgrNodePGconn(MgrNodeWrapper *mgrNode,
								  bool isMaster,
								  PGconn **connP,
								  bool complain);
static bool refreshSyncStandbyNames(SwitcherNodeWrapper *masterNode,
									MgrNodeWrapper *faultSlaveNode);
static bool pullBackToCluster(SwitcherNodeWrapper *masterNode,
							  MgrNodeWrapper *faultSlaveNode,
							  bool masterNodeFailed);
static void callAppendCoordinatorFor(MgrNodeWrapper *destCoordinator,
									 MgrNodeWrapper *srcCoordinator);
static void callAppendActivateCoordinator(MgrNodeWrapper *destCoordinator);
static void checkMgrNodeDataInDB(MgrNodeWrapper *nodeDataInMem);
static void getCheckMgrNodesForRepairer(dlist_head *mgrNodes);
static void refreshMgrNodeBeforeRepair(MgrNodeWrapper *mgrNode,
									   MemoryContext spiContext);
static void refreshMgrNodeAfterRepair(MgrNodeWrapper *mgrNode,
									  MemoryContext spiContext);
// static void refreshMgrNodeBeforeAppendNode(MgrNodeWrapper *mgrNode,
// 										   MemoryContext spiContext);
// static void refreshMgrNodeAfterAppendNode(MgrNodeWrapper *mgrNode,
// 										  MemoryContext spiContext);
static void selectSiblingActiveNodes(MgrNodeWrapper *faultNode,
									 dlist_head *resultList,
									 MemoryContext spiContext);
static void updateMgrNodeNodesync(MgrNodeWrapper *mgrNode,
								  MemoryContext spiContext);
static bool checkGetMasterNode(MgrNodeWrapper *faultSlaveNode,
							   SwitcherNodeWrapper **masterNodeP);
static RepairerConfiguration *newRepairerConfiguration(AdbDoctorConf *conf);
static void examineAdbDoctorConf(dlist_head *mgrNodes);
static void resetRepairer(void);

static void handleSigterm(SIGNAL_ARGS);
static void handleSigusr1(SIGNAL_ARGS);

static AdbDoctorConfShm *confShm;
static RepairerConfiguration *repairerConfiguration;
static sigjmp_buf reset_repairer_sigjmp_buf;

static volatile sig_atomic_t gotSigterm = false;
static volatile sig_atomic_t gotSigusr1 = false;

void adbDoctorRepairerMain(Datum main_arg)
{
	AdbDoctorBgworkerData *bgworkerData;
	AdbDoctorConf *confInLocal;
	dlist_head mgrNodes = DLIST_STATIC_INIT(mgrNodes);

	pqsignal(SIGTERM, handleSigterm);
	pqsignal(SIGUSR1, handleSigusr1);
	BackgroundWorkerUnblockSignals();
	BackgroundWorkerInitializeConnection(DEFAULT_DB, NULL, 0);

	PG_TRY();
	{
		bgworkerData = attachAdbDoctorBgworkerDataShm(main_arg,
													  MyBgworkerEntry->bgw_name);
		notifyAdbDoctorRegistrant();
		ereport(LOG,
				(errmsg("%s started",
						MyBgworkerEntry->bgw_name)));

		confShm = attachAdbDoctorConfShm(bgworkerData->commonShmHandle,
										 MyBgworkerEntry->bgw_name);
		confInLocal = copyAdbDoctorConfFromShm(confShm);
		repairerConfiguration = newRepairerConfiguration(confInLocal);
		pfree(confInLocal);

		if (sigsetjmp(reset_repairer_sigjmp_buf, 1) != 0)
		{
			pfreeMgrNodeWrapperList(&mgrNodes, NULL);
		}
		dlist_init(&mgrNodes);

		getCheckMgrNodesForRepairer(&mgrNodes);
		repairerMainLoop(&mgrNodes);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();
	proc_exit(1);
}

static void repairerMainLoop(dlist_head *mgrNodes)
{
	int rc;

	while (!gotSigterm)
	{
		checkAndRepairNodes(mgrNodes);

		if (dlist_is_empty(mgrNodes))
		{
			/* The switch task was completed, the process should exits */
			break;
		}
		CHECK_FOR_INTERRUPTS();
		set_ps_display("sleeping", false);
		rc = WaitLatchOrSocket(MyLatch,
							   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
							   PGINVALID_SOCKET,
							   repairerConfiguration->repairIntervalMs,
							   PG_WAIT_EXTENSION);
		/* Reset the latch, bail out if postmaster died. */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);
		/* Interrupted? */
		if (rc & WL_LATCH_SET)
		{
			ResetLatch(MyLatch);
		}
		CHECK_FOR_INTERRUPTS();
		examineAdbDoctorConf(mgrNodes);
	}
}

static void checkAndRepairNodes(dlist_head *faultNodes)
{
	MemoryContext oldContext;
	MemoryContext workerContext;
	bool pgxcNodeCleaned;

	oldContext = CurrentMemoryContext;
	workerContext = AllocSetContextCreate(oldContext,
										  "workerContext",
										  ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(workerContext);

	PG_TRY();
	{
		/* repaire gtmcoord slave first */
		repairSpecificMgrNode(faultNodes, CNDN_TYPE_GTM_COOR_SLAVE);

		pgxcNodeCleaned = cleanFaultNodesOnCoordinators(faultNodes);
		CHECK_FOR_INTERRUPTS();

		if (pgxcNodeCleaned)
		{
			repairSpecificMgrNode(faultNodes, CNDN_TYPE_DATANODE_SLAVE);
			CHECK_FOR_INTERRUPTS();
			repairSpecificMgrNode(faultNodes, CNDN_TYPE_COORDINATOR_MASTER);
		}
	}
	PG_CATCH();
	{
		EmitErrorReport();
		FlushErrorState();
		MemoryContextSwitchTo(oldContext);
	}
	PG_END_TRY();

	(void)MemoryContextSwitchTo(oldContext);
	MemoryContextDelete(workerContext);
}

static void repairSpecificMgrNode(dlist_head *mgrNodes, char nodetype)
{
	dlist_mutable_iter iter;
	MgrNodeWrapper *mgrNode;
	bool repaired;
	MemoryContext oldContext;

	oldContext = CurrentMemoryContext;
	dlist_foreach_modify(iter, mgrNodes)
	{
		CHECK_FOR_INTERRUPTS();
		mgrNode = dlist_container(MgrNodeWrapper, link, iter.cur);
		if (mgrNode->form.nodetype == nodetype)
		{
			MemoryContextSwitchTo(oldContext);
			repaired = false;
			if (mgrNode->form.nodetype == CNDN_TYPE_GTM_COOR_SLAVE)
			{
				repaired = repairSlaveNode(mgrNode);
			}
			else if (mgrNode->form.nodetype == CNDN_TYPE_COORDINATOR_MASTER)
			{
				repaired = repairCoordinatorNode(mgrNode);
			}
			else if (mgrNode->form.nodetype == CNDN_TYPE_DATANODE_SLAVE)
			{
				repaired = repairSlaveNode(mgrNode);
			}
			else
			{
				ereport(WARNING,
						(errmsg("%s repaire failed, Unsupported nodetype %c",
								NameStr(mgrNode->form.nodename),
								mgrNode->form.nodetype)));
			}
			if (repaired)
			{
				dlist_delete(iter.cur);
				pfreeMgrNodeWrapper(mgrNode);
			}
		}
		else
		{
			continue;
		}
	}
}

static bool cleanFaultNodesOnCoordinators(dlist_head *faultNodes)
{
	dlist_head coordinators = DLIST_STATIC_INIT(coordinators);
	dlist_iter iterCoord;
	dlist_mutable_iter iterFault;
	MgrNodeWrapper *coordinator;
	MgrNodeWrapper *faultNode;
	MgrNodeWrapper *needCleanNode;
	PGconn *coordConn = NULL;
	dlist_head needCleanNodes = DLIST_STATIC_INIT(needCleanNodes);
	bool allCleaned;
	MemoryContext oldContext;
	MemoryContext spiContext;
	int spiRes;

	oldContext = CurrentMemoryContext;
	SPI_CONNECT_TRANSACTIONAL_START(spiRes, true);
	spiContext = CurrentMemoryContext;
	MemoryContextSwitchTo(oldContext);
	selectActiveMasterCoordinators(spiContext, &coordinators);
	SPI_FINISH_TRANSACTIONAL_COMMIT();

	if (dlist_is_empty(&coordinators))
	{
		ereport(ERROR,
				(errmsg("can't find any active coordinators")));
	}

	dlist_foreach_modify(iterFault, faultNodes)
	{
		faultNode = dlist_container(MgrNodeWrapper, link, iterFault.cur);
		if (isGtmCoordMgrNode(faultNode->form.nodetype))
		{
			ereport(LOG,
					(errmsg("%s is a gtm coordinator type node, no need to clean pgxc_node",
							NameStr(faultNode->form.nodename))));
		}
		else if (isDataNodeMgrNode(faultNode->form.nodetype) ||
				 isCoordinatorMgrNode(faultNode->form.nodetype))
		{
			needCleanNode = palloc0(sizeof(MgrNodeWrapper));
			memcpy(needCleanNode, faultNode, sizeof(MgrNodeWrapper));
			dlist_push_tail(&needCleanNodes, &needCleanNode->link);
		}
		else
		{
			ereport(ERROR,
					(errmsg("unsupported node %s with nodetype %c",
							NameStr(faultNode->form.nodename),
							faultNode->form.nodetype)));
		}
	}

	if (dlist_is_empty(&needCleanNodes))
	{
		allCleaned = true;
		ereport(LOG,
				(errmsg("no need to clean fault nodes in table pgxc_node of coordinators")));
	}
	else
	{
		allCleaned = true;
		dlist_foreach(iterCoord, &coordinators)
		{
			coordinator = dlist_container(MgrNodeWrapper, link, iterCoord.cur);
			if (checkGetMgrNodePGconn(coordinator, true, &coordConn, false))
			{
				dlist_foreach_modify(iterFault, &needCleanNodes)
				{
					needCleanNode = dlist_container(MgrNodeWrapper, link, iterFault.cur);
					checkMgrNodeDataInDB(needCleanNode);
				}
				cleanMgrNodesOnCoordinator(&needCleanNodes,
										   coordinator,
										   coordConn,
										   true);
			}
			else
			{
				allCleaned = false;
			}
			if (coordConn)
			{
				PQfinish(coordConn);
				coordConn = NULL;
			}
		}
		dlist_foreach_modify(iterFault, &needCleanNodes)
		{
			needCleanNode = dlist_container(MgrNodeWrapper, link, iterFault.cur);
			dlist_delete(iterFault.cur);
			pfree(needCleanNode);
		}
		if (!allCleaned)
			ereport(WARNING,
					(errmsg("clean fault nodes in table pgxc_node of some coordinators failed")));
	}
	return allCleaned;
}

static bool repairCoordinatorNode(MgrNodeWrapper *faultCoordinator)
{
	int spiRes;
	volatile bool done = false;
	MemoryContext oldContext;
	MemoryContext spiContext;
	GetAgentCmdRst getAgentCmdRst;
	MgrNodeWrapper mgrNodeBackup;
	MgrNodeWrapper *srcCoordinator = NULL;
	SwitcherNodeWrapper *activeCoordinator = NULL;
	SwitcherNodeWrapper *gtmCoordMaster = NULL;
	dlist_head activeCoordinators = DLIST_STATIC_INIT(activeCoordinators);
	dlist_iter iter;
	int numOfOrdinaryCoordinators;

	memcpy(&mgrNodeBackup, faultCoordinator, sizeof(MgrNodeWrapper));

	oldContext = CurrentMemoryContext;
	SPI_CONNECT_TRANSACTIONAL_START(spiRes, true);
	spiContext = CurrentMemoryContext;
	MemoryContextSwitchTo(oldContext);

	PG_TRY();
	{
		set_ps_display(NameStr(faultCoordinator->form.nodename), false);

		checkGetMasterCoordinators(spiContext,
								   &activeCoordinators,
								   true,
								   true);
		numOfOrdinaryCoordinators = 0;
		dlist_foreach(iter, &activeCoordinators)
		{
			activeCoordinator = dlist_container(SwitcherNodeWrapper, link, iter.cur);
			if (activeCoordinator->mgrNode->form.nodetype == CNDN_TYPE_COORDINATOR_MASTER)
			{
				srcCoordinator = activeCoordinator->mgrNode;
				numOfOrdinaryCoordinators++;
			}
			if (activeCoordinator->mgrNode->form.nodetype == CNDN_TYPE_GTM_COOR_MASTER)
			{
				gtmCoordMaster = activeCoordinator;
			}
		}
		if (numOfOrdinaryCoordinators < 1)
		{
			srcCoordinator = gtmCoordMaster->mgrNode;
		}
		if (!gtmCoordMaster)
		{
			ereport(ERROR,
					(errmsg("can't find any gtm coordinator master")));
		}
		refreshMgrNodeBeforeRepair(faultCoordinator,
								   spiContext);
		/* shutdown fault node */
		shutdownNodeWithinSeconds(faultCoordinator,
								  SHUTDOWN_NODE_FAST_SECONDS,
								  SHUTDOWN_NODE_IMMEDIATE_SECONDS,
								  true);
		/* clean fault node */
		mgr_clean_node_folder(AGT_CMD_CLEAN_NODE,
							  faultCoordinator->form.nodehost,
							  faultCoordinator->nodepath,
							  &getAgentCmdRst);
		if (true == getAgentCmdRst.ret)
			ereport(LOG,
					(errmsg("try clean node %s successed",
							NameStr(faultCoordinator->form.nodename))));
		else
			ereport(ERROR,
					(errmsg("try clean node %s failed, %s",
							NameStr(faultCoordinator->form.nodename),
							getAgentCmdRst.description.data)));
		pfree(getAgentCmdRst.description.data);

		callAppendCoordinatorFor(faultCoordinator,
								 srcCoordinator);
		callAppendActivateCoordinator(faultCoordinator);

		refreshMgrNodeAfterRepair(faultCoordinator,
								  spiContext);

		ereport(LOG,
				(errmsg("%s has been successfully repaired",
						NameStr(faultCoordinator->form.nodename))));
		done = true;
	}
	PG_CATCH();
	{
		done = false;
		EmitErrorReport();
		FlushErrorState();
		MemoryContextSwitchTo(oldContext);
	}
	PG_END_TRY();

	if (done)
	{
		SPI_FINISH_TRANSACTIONAL_COMMIT();
	}
	else
	{
		memcpy(faultCoordinator, &mgrNodeBackup, sizeof(MgrNodeWrapper));
		SPI_FINISH_TRANSACTIONAL_ABORT();
	}
	pfreeSwitcherNodeWrapperList(&activeCoordinators, NULL);

	return done;
}

// static void tryReEnableCoordinator(MgrNodeWrapper *faultCoordinator,
// 								   dlist_head *activeCoordinators,
// 								   MemoryContext spiContext)
// {
// 	PGconn *faultNodePgconn = NULL;
// 	char *faultNodePgxcNodeName = NULL;
// 	bool tryedLockCluster = false;
// 	char *sql = false;
// 	SwitcherNodeWrapper *holdLockCoordinator;
// 	SwitcherNodeWrapper *activeCoordinator;
// 	dlist_iter iter;
// 	MemoryContext oldContext;
// 	ErrorData *edata = NULL;

// 	oldContext = CurrentMemoryContext;
// 	ereport(LOG,
// 			(errmsg("can't find any active coordinator, try to startup %s",
// 					NameStr(faultCoordinator->form.nodename))));

// 	PG_TRY();
// 	{
// 		refreshMgrNodeBeforeRepair(faultCoordinator,
// 								   spiContext);
// 		startupNodeWithinSeconds(faultCoordinator,
// 								 STARTUP_NODE_SECONDS,
// 								 true);

// 		checkGetMgrNodePGconn(faultCoordinator,
// 							  true,
// 							  &faultNodePgconn,
// 							  true);
// 		/* clean pgxc_node in fault coordinator */
// 		sql = psprintf("delete from pgxc_node where node_name !='%s' and nodeis_gtm !=%d::boolean ",
// 					   NameStr(faultCoordinator->form.nodename),
// 					   true);
// 		PQexecCommandSql(faultNodePgconn, sql, true);
// 		faultNodePgxcNodeName = getMgrNodePgxcNodeName(faultCoordinator,
// 													   faultNodePgconn,
// 													   false, true);
// 		pgxcPoolReloadOnCoordinator(faultNodePgconn,
// 									true,
// 									faultNodePgxcNodeName,
// 									true);

// 		tryLockCluster(activeCoordinators);
// 		tryedLockCluster = true;
// 		holdLockCoordinator = getHoldLockCoordinator(activeCoordinators);

// 		PQexecCommandSql(holdLockCoordinator->pgConn,
// 						 "set xc_maintenance_mode = on;",
// 						 true);

// 		/* add this fault node to the active coordinator which hold the cluster lock */
// 		compareAndCreateMgrNodeOnCoordinator(faultCoordinator,
// 											 faultNodePgxcNodeName,
// 											 holdLockCoordinator->mgrNode,
// 											 holdLockCoordinator->pgConn,
// 											 true,
// 											 holdLockCoordinator->pgxcNodeName,
// 											 true);
// 		dlist_foreach(iter, activeCoordinators)
// 		{
// 			activeCoordinator = dlist_container(SwitcherNodeWrapper, link, iter.cur);
// 			/* add the active coordinator to this fault node */
// 			compareAndCreateMgrNodeOnCoordinator(activeCoordinator->mgrNode,
// 												 activeCoordinator->pgxcNodeName,
// 												 faultCoordinator,
// 												 holdLockCoordinator->pgConn,
// 												 false,
// 												 faultNodePgxcNodeName,
// 												 true);
// 			/* add this fault node to the active coordinator */
// 			compareAndCreateMgrNodeOnCoordinator(faultCoordinator,
// 												 faultNodePgxcNodeName,
// 												 activeCoordinator->mgrNode,
// 												 holdLockCoordinator->pgConn,
// 												 activeCoordinator == holdLockCoordinator,
// 												 activeCoordinator->pgxcNodeName,
// 												 true);
// 		}
// 		/* add the data node master to the pgxc_node table of this fault node */
// 		addDataNodeMasterToCoordinator(faultCoordinator,
// 									   holdLockCoordinator->pgConn,
// 									   false,
// 									   faultNodePgxcNodeName,
// 									   spiContext);
// 		refreshMgrNodeAfterRepair(faultCoordinator,
// 								  spiContext);

// 		tryUnlockCluster(activeCoordinators, true);
// 	}
// 	PG_CATCH();
// 	{
// 		/* Save error info in our stmt_mcontext */
// 		MemoryContextSwitchTo(oldContext);
// 		edata = CopyErrorData();
// 		FlushErrorState();
// 		if (tryedLockCluster)
// 		{
// 			tryUnlockCluster(activeCoordinators, false);
// 			/* clean the fault coordinator in pgxc_node of activecoordinators */
// 			dlist_foreach(iter, activeCoordinators)
// 			{
// 				activeCoordinator = dlist_container(SwitcherNodeWrapper, link, iter.cur);
// 				compareAndDropMgrNodeOnCoordinator(faultCoordinator,
// 													activeCoordinator->mgrNode,
// 													activeCoordinator->pgConn,
// 													true,
// 													activeCoordinator->pgxcNodeName,
// 													false);
// 			}
// 			shutdownNodeWithinSeconds(faultCoordinator,
// 									  SHUTDOWN_NODE_FAST_SECONDS,
// 									  SHUTDOWN_NODE_IMMEDIATE_SECONDS,
// 									  false);
// 		}
// 	}
// 	PG_END_TRY();

// 	if (sql)
// 		pfree(sql);
// 	if (faultNodePgconn)
// 		PQfinish(faultNodePgconn);
// 	if (faultNodePgxcNodeName)
// 		pfree(faultNodePgxcNodeName);
// 	(void)MemoryContextSwitchTo(oldContext);
// 	if (edata)
// 	{
// 		ReThrowError(edata);
// 	}
// }

static bool repairSlaveNode(MgrNodeWrapper *faultSlaveNode)
{
	volatile bool done = false;
	MgrNodeWrapper mgrNodeBackup;
	SwitcherNodeWrapper *masterNode = NULL;
	MemoryContext oldContext;

	oldContext = CurrentMemoryContext;
	memcpy(&mgrNodeBackup, faultSlaveNode, sizeof(MgrNodeWrapper));

	PG_TRY();
	{
		set_ps_display(NameStr(faultSlaveNode->form.nodename), false);

		if (checkGetMasterNode(faultSlaveNode, &masterNode))
		{
			if (!refreshSyncStandbyNames(masterNode, faultSlaveNode))
				ereport(ERROR,
						(errmsg("refresh master node %s synchronous_standby_names failed",
								NameStr(masterNode->mgrNode->form.nodename))));
			if (!pullBackToCluster(masterNode, faultSlaveNode, false))
				ereport(ERROR,
						(errmsg("pull %s back to cluster failed",
								NameStr(faultSlaveNode->form.nodename))));
		}
		else
		{
			/* 
			 * The old master node is invalid. You need to try to pull this  
			 * node directly back to the cluster. If you don't do this, the 
			 * master/slave switching may not succeed. 
			 */
			if (!pullBackToCluster(masterNode, faultSlaveNode, true))
				ereport(ERROR,
						(errmsg("pull %s back to cluster failed",
								NameStr(faultSlaveNode->form.nodename))));
		}
		done = true;
		ereport(LOG,
				(errmsg("%s has been successfully repaired",
						NameStr(faultSlaveNode->form.nodename))));
	}
	PG_CATCH();
	{
		done = false;
		EmitErrorReport();
		FlushErrorState();
		MemoryContextSwitchTo(oldContext);
	}
	PG_END_TRY();

	pfreeSwitcherNodeWrapper(masterNode);
	if (!done)
	{
		memcpy(faultSlaveNode, &mgrNodeBackup, sizeof(MgrNodeWrapper));
	}
	return done;
}

// static void addDataNodeMasterToCoordinator(MgrNodeWrapper *coordinator,
// 										   PGconn *activeConn,
// 										   bool localExecute,
// 										   char *executeOnNodeName,
// 										   MemoryContext spiContext)
// {
// 	dlist_iter iter;
// 	dlist_head mgrNodes = DLIST_STATIC_INIT(mgrNodes);
// 	MgrNodeWrapper *mgrNode;
// 	char *pgxcNodeName;

// 	selectActiveMgrNodeByNodetype(spiContext,
// 								  CNDN_TYPE_DATANODE_MASTER,
// 								  &mgrNodes);
// 	dlist_foreach(iter, &mgrNodes)
// 	{
// 		mgrNode = dlist_container(MgrNodeWrapper, link, iter.cur);
// 		pgxcNodeName = getMgrNodePgxcNodeName(mgrNode, NULL, false, true);
// 		compareAndCreateMgrNodeOnCoordinator(mgrNode,
// 											 pgxcNodeName,
// 											 coordinator,
// 											 activeConn,
// 											 localExecute,
// 											 executeOnNodeName,
// 											 true);
// 		pfree(pgxcNodeName);
// 	}

// dlist_init(&mgrNodes);
// selectActiveMgrNodeByNodetype(spiContext, CNDN_TYPE_DATANODE_MASTER, &mgrNodes);
// dlist_foreach(iter, &mgrNodes)
// {
// 	mgrNode = dlist_container(MgrNodeWrapper, link, iter.cur);
// 	compareAndCreateMgrNodeOnCoordinator(mgrNode,
// 										 coordinator,
// 										 activeConn,
// 										 localExecute,
// 										 executeOnNodeName,
// 										 true);
// }
//}

static bool checkGetMgrNodePGconn(MgrNodeWrapper *mgrNode,
								  bool isMaster,
								  PGconn **connP,
								  bool complain)
{
	if (*connP)
	{
		PQfinish(*connP);
		*connP = NULL;
	}
	*connP = getNodeDefaultDBConnection(mgrNode, 10);
	if (*connP == NULL)
	{
		ereport(complain ? ERROR : LOG,
				(errmsg("connect to %s failed",
						NameStr(mgrNode->form.nodename))));
		return false;
	}
	if (!checkNodeRunningMode(*connP, isMaster))
	{
		PQfinish(*connP);
		*connP = NULL;
		ereport(complain ? ERROR : LOG,
				(errmsg("%s configured as %s, but actually did not running on that status",
						isMaster ? "master" : "slave",
						NameStr(mgrNode->form.nodename))));
		return false;
	}
	return true;
}

static bool refreshSyncStandbyNames(SwitcherNodeWrapper *masterNode,
									MgrNodeWrapper *faultSlaveNode)
{
	char *temp;
	char *oldSyncStandbyNames = NULL;
	char *newSyncStandbyNames = NULL;
	char *bareNodenames;
	StringInfoData buf;
	int i;
	dlist_iter iter;
	MgrNodeWrapper *node;
	int spiRes;
	volatile bool done = false;
	MemoryContext oldContext;
	MemoryContext spiContext;
	MgrNodeWrapper mgrNodeBackup;
	dlist_head siblingNodes = DLIST_STATIC_INIT(siblingNodes);

	memcpy(&mgrNodeBackup, faultSlaveNode, sizeof(MgrNodeWrapper));

	oldContext = CurrentMemoryContext;
	SPI_CONNECT_TRANSACTIONAL_START(spiRes, true);
	spiContext = CurrentMemoryContext;

	initStringInfo(&buf);
	PG_TRY();
	{
		selectSiblingActiveNodes(faultSlaveNode, &siblingNodes, spiContext);
		oldSyncStandbyNames = showNodeParameter(masterNode->pgConn,
												"synchronous_standby_names", true);
		ereport(DEBUG1,
				(errmsg("%s synchronous_standby_names is %s",
						NameStr(masterNode->mgrNode->form.nodename),
						oldSyncStandbyNames)));
		oldSyncStandbyNames = trimString(oldSyncStandbyNames);
		if (oldSyncStandbyNames == NULL || strlen(oldSyncStandbyNames) == 0)
		{
			done = true;
		}
		else
		{
			temp = palloc0(strlen(oldSyncStandbyNames) + 1);
			/* "FIRST 1 (nodename2,nodename4)" will get result nodename2,nodename4 */
			sscanf(oldSyncStandbyNames, "%*[^(](%[^)]", temp);
			bareNodenames = trimString(temp);
			pfree(temp);
			if (bareNodenames == NULL || strlen(bareNodenames) == 0)
			{
				done = true;
			}
			else
			{
				i = 0;
				/* function "strtok" will scribble on the input argument */
				temp = strtok(bareNodenames, ",");
				while (temp)
				{
					if (!equalsAfterTrim(temp, NameStr(faultSlaveNode->form.nodename)))
					{
						i++;
						if (i == 1)
							appendStringInfo(&buf, "%s", temp);
						else
							appendStringInfo(&buf, ",%s", temp);
						dlist_foreach(iter, &siblingNodes)
						{
							node = dlist_container(MgrNodeWrapper, link, iter.cur);
							if (strcmp(NameStr(node->form.nodename), temp) == 0)
							{
								if (i == 1)
									namestrcpy(&node->form.nodesync,
											   getMgrNodeSyncStateValue(SYNC_STATE_SYNC));
								else
									namestrcpy(&node->form.nodesync,
											   getMgrNodeSyncStateValue(SYNC_STATE_POTENTIAL));
							}
						}
					}
					temp = strtok(NULL, ",");
				}
				if (buf.data == NULL || strlen(buf.data) == 0)
				{
					newSyncStandbyNames = palloc0(1);
				}
				else
				{
					newSyncStandbyNames = psprintf("FIRST %d (%s)", 1, buf.data);
				}
				if (strcmp(oldSyncStandbyNames, newSyncStandbyNames) != 0)
				{
					dlist_foreach(iter, &siblingNodes)
					{
						node = dlist_container(MgrNodeWrapper, link, iter.cur);
						updateMgrNodeNodesync(node, spiContext);
					}
					namestrcpy(&faultSlaveNode->form.nodesync, "");
					updateMgrNodeNodesync(faultSlaveNode, spiContext);
					ereport(LOG,
							(errmsg("%s try to change synchronous_standby_names from '%s' to '%s'",
									NameStr(masterNode->mgrNode->form.nodename),
									oldSyncStandbyNames,
									newSyncStandbyNames)));
					setCheckSynchronousStandbyNames(masterNode->mgrNode,
													masterNode->pgConn,
													newSyncStandbyNames,
													CHECK_SYNC_STANDBY_NAMES_SECONDS);
				}
				else
				{
					/* Synchronous_standby_names has been set and does not need to be set repeatedly */
				}
				done = true;
			}
		}
	}
	PG_CATCH();
	{
		done = false;
		EmitErrorReport();
		FlushErrorState();
		MemoryContextSwitchTo(oldContext);
	}
	PG_END_TRY();

	if (done)
	{
		SPI_FINISH_TRANSACTIONAL_COMMIT();
	}
	else
	{
		memcpy(faultSlaveNode, &mgrNodeBackup, sizeof(MgrNodeWrapper));
		SPI_FINISH_TRANSACTIONAL_ABORT();
	}
	return done;
}

static bool pullBackToCluster(SwitcherNodeWrapper *masterNode,
							  MgrNodeWrapper *faultSlaveNode,
							  bool masterNodeFailed)
{
	int spiRes;
	volatile bool done = false;
	MemoryContext spiContext;
	MgrNodeWrapper mgrNodeBackup;
	MemoryContext oldContext;

	oldContext = CurrentMemoryContext;
	memcpy(&mgrNodeBackup, faultSlaveNode, sizeof(MgrNodeWrapper));

	SPI_CONNECT_TRANSACTIONAL_START(spiRes, true);
	spiContext = CurrentMemoryContext;
	PG_TRY();
	{
		refreshMgrNodeBeforeRepair(faultSlaveNode, spiContext);

		startupNodeWithinSeconds(faultSlaveNode,
								 STARTUP_NODE_SECONDS, true);
		if (masterNodeFailed)
		{
			ereport(WARNING,
					(errmsg("successfully started the standby node %s, but it's master %s failed",
							NameStr(faultSlaveNode->form.nodename),
							NameStr(masterNode->mgrNode->form.nodename))));
		}
		else
		{
			appendSlaveNodeFollowMaster(masterNode->mgrNode,
										faultSlaveNode, masterNode->pgConn);
		}
		refreshMgrNodeAfterRepair(faultSlaveNode, spiContext);
		done = true;
	}
	PG_CATCH();
	{
		done = false;
		EmitErrorReport();
		FlushErrorState();
		MemoryContextSwitchTo(oldContext);
	}
	PG_END_TRY();

	if (done)
	{
		SPI_FINISH_TRANSACTIONAL_COMMIT();
	}
	else
	{
		memcpy(faultSlaveNode, &mgrNodeBackup, sizeof(MgrNodeWrapper));
		SPI_FINISH_TRANSACTIONAL_ABORT();
	}
	return done;
}

static void callAppendCoordinatorFor(MgrNodeWrapper *destCoordinator,
									 MgrNodeWrapper *srcCoordinator)
{
	HeapTupleHeader tupleHeader;
	HeapTupleData tuple;
	TupleDesc tupdesc = NULL;
	Datum datum;
	bool isnull;

	tupleHeader =
		DatumGetHeapTupleHeader(
			DirectFunctionCall2(mgr_append_coord_to_coord,
								CStringGetDatum(NameStr(srcCoordinator->form.nodename)),
								CStringGetDatum(NameStr(destCoordinator->form.nodename))));
	tupdesc = lookup_rowtype_tupdesc_copy(HeapTupleHeaderGetTypeId(tupleHeader),
										  HeapTupleHeaderGetTypMod(tupleHeader));
	tuple.t_len = HeapTupleHeaderGetDatumLength(tupleHeader);
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_data = tupleHeader;
	datum = fastgetattr(&tuple, 2, tupdesc, &isnull);
	if (isnull)
	{
		ereport(ERROR,
				(errmsg("try append %s for %s failed, null return",
						NameStr(destCoordinator->form.nodename),
						NameStr(srcCoordinator->form.nodename))));
	}
	else
	{
		if (DatumGetBool(datum))
		{
			ereport(LOG,
					(errmsg("try append %s for %s successed",
							NameStr(destCoordinator->form.nodename),
							NameStr(srcCoordinator->form.nodename))));
		}
		else
		{
			datum = fastgetattr(&tuple, 3, tupdesc, &isnull);
			ereport(ERROR,
					(errmsg("try append %s for %s failed, %s",
							NameStr(destCoordinator->form.nodename),
							NameStr(srcCoordinator->form.nodename),
							isnull ? "unknow reason" : DatumGetCString(datum))));
		}
	}
}

static void callAppendActivateCoordinator(MgrNodeWrapper *destCoordinator)
{
	HeapTupleHeader tupleHeader;
	HeapTupleData tuple;
	TupleDesc tupdesc = NULL;
	Datum datum;
	bool isnull;

	tupleHeader =
		DatumGetHeapTupleHeader(
			DirectFunctionCall1(mgr_append_activate_coord,
								CStringGetDatum(NameStr(destCoordinator->form.nodename))));
	tupdesc = lookup_rowtype_tupdesc_copy(HeapTupleHeaderGetTypeId(tupleHeader),
										  HeapTupleHeaderGetTypMod(tupleHeader));
	tuple.t_len = HeapTupleHeaderGetDatumLength(tupleHeader);
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_data = tupleHeader;
	datum = fastgetattr(&tuple, 2, tupdesc, &isnull);
	if (isnull)
	{
		ereport(ERROR,
				(errmsg("try append activate %s failed, null return",
						NameStr(destCoordinator->form.nodename))));
	}
	else
	{
		if (DatumGetBool(datum))
		{
			ereport(LOG,
					(errmsg("try append activate %s successed",
							NameStr(destCoordinator->form.nodename))));
		}
		else
		{
			datum = fastgetattr(&tuple, 3, tupdesc, &isnull);
			ereport(ERROR,
					(errmsg("try append activate %s failed, %s",
							NameStr(destCoordinator->form.nodename),
							isnull ? "unknow reason" : DatumGetCString(datum))));
		}
	}
}

static void checkMgrNodeDataInDB(MgrNodeWrapper *nodeDataInMem)
{
	MemoryContext oldContext;
	MemoryContext spiContext;
	int ret;
	MgrNodeWrapper *nodeDataInDB = NULL;

	oldContext = CurrentMemoryContext;
	SPI_CONNECT_TRANSACTIONAL_START(ret, true);
	spiContext = CurrentMemoryContext;
	MemoryContextSwitchTo(oldContext);
	nodeDataInDB = selectMgrNodeByOid(nodeDataInMem->oid, spiContext);
	SPI_FINISH_TRANSACTIONAL_COMMIT();

	if (!nodeDataInDB)
	{
		ereport(ERROR,
				(errmsg("%s %s, data not exists in database",
						MyBgworkerEntry->bgw_name,
						NameStr(nodeDataInDB->form.nodename))));
	}
	if (!nodeDataInDB->form.allowcure)
	{
		ereport(ERROR,
				(errmsg("%s %s, cure not allowed",
						MyBgworkerEntry->bgw_name,
						NameStr(nodeDataInDB->form.nodename))));
	}
	if (pg_strcasecmp(NameStr(nodeDataInDB->form.curestatus),
					  CURE_STATUS_ISOLATED) != 0)
	{
		ereport(ERROR,
				(errmsg("%s %s, curestatus:%s, it is not my duty",
						MyBgworkerEntry->bgw_name,
						NameStr(nodeDataInDB->form.nodename),
						NameStr(nodeDataInDB->form.curestatus))));
	}
	if (pg_strcasecmp(NameStr(nodeDataInMem->form.curestatus),
					  NameStr(nodeDataInDB->form.curestatus)) != 0)
	{
		ereport(ERROR,
				(errmsg("%s %s, curestatus not matched, in memory:%s, but in database:%s",
						MyBgworkerEntry->bgw_name,
						NameStr(nodeDataInDB->form.nodename),
						NameStr(nodeDataInMem->form.curestatus),
						NameStr(nodeDataInDB->form.curestatus))));
	}
	if (!isIdenticalDoctorMgrNode(nodeDataInMem, nodeDataInDB))
	{
		ereport(ERROR,
				(errmsg("%s %s, data has changed in database",
						MyBgworkerEntry->bgw_name,
						NameStr(nodeDataInDB->form.nodename))));
	}
	pfreeMgrNodeWrapper(nodeDataInDB);
	nodeDataInDB = NULL;
}

static void getCheckMgrNodesForRepairer(dlist_head *mgrNodes)
{
	MemoryContext oldContext;
	MemoryContext spiContext;
	int ret;

	oldContext = CurrentMemoryContext;
	SPI_CONNECT_TRANSACTIONAL_START(ret, true);
	spiContext = CurrentMemoryContext;
	MemoryContextSwitchTo(oldContext);
	selectMgrNodesForRepairerDoctor(spiContext, mgrNodes);
	SPI_FINISH_TRANSACTIONAL_COMMIT();
	if (dlist_is_empty(mgrNodes))
	{
		ereport(ERROR,
				(errmsg("%s There is no node to repair",
						MyBgworkerEntry->bgw_name)));
	}
}

static void refreshMgrNodeBeforeRepair(MgrNodeWrapper *mgrNode,
									   MemoryContext spiContext)
{
	int rows;
	rows = updateMgrNodeCurestatus(mgrNode, CURE_STATUS_CURING, spiContext);
	if (rows != 1)
	{
		ereport(ERROR,
				(errmsg("%s, can not transit to curestatus:%s",
						NameStr(mgrNode->form.nodename),
						CURE_STATUS_CURING)));
	}
	else
	{
		namestrcpy(&mgrNode->form.curestatus, CURE_STATUS_CURING);
	}
}

static void refreshMgrNodeAfterRepair(MgrNodeWrapper *mgrNode,
									  MemoryContext spiContext)
{
	int rows;
	rows = updateMgrNodeToUnIsolate(mgrNode, spiContext);
	if (rows != 1)
	{
		ereport(ERROR,
				(errmsg("%s, can not transit to curestatus:%s",
						NameStr(mgrNode->form.nodename),
						CURE_STATUS_CURING)));
	}
}

// static void refreshMgrNodeBeforeAppendNode(MgrNodeWrapper *mgrNode,
// 										   MemoryContext spiContext)
// {
// 	StringInfoData buf;
// 	NameData oldCurestatus;

// 	namestrcpy(&oldCurestatus, NameStr(mgrNode->form.curestatus));
// 	namestrcpy(&mgrNode->form.curestatus, CURE_STATUS_CURING);
// 	mgrNode->form.nodeinited = false;
// 	mgrNode->form.nodeincluster = false;

// 	initStringInfo(&buf);
// 	appendStringInfo(&buf,
// 					 "update pg_catalog.mgr_node "
// 					 "set curestatus = '%s', "
// 					 "nodeinited = %d::boolean, "
// 					 "nodeincluster = %d::boolean "
// 					 "WHERE oid = %u "
// 					 "and curestatus = '%s' "
// 					 "and nodetype = '%c' ",
// 					 NameStr(mgrNode->form.curestatus),
// 					 mgrNode->form.nodeinited,
// 					 mgrNode->form.nodeincluster,
// 					 mgrNode->oid,
// 					 NameStr(oldCurestatus),
// 					 mgrNode->form.nodetype);
// 	if (executeUpdateSql(buf.data, spiContext) != 1)
// 	{
// 		ereport(ERROR,
// 				(errmsg("%s, can not transit to curestatus:%s",
// 						NameStr(mgrNode->form.nodename),
// 						NameStr(mgrNode->form.curestatus))));
// 	}
// }

// static void refreshMgrNodeAfterAppendNode(MgrNodeWrapper *mgrNode,
// 										  MemoryContext spiContext)
// {
// 	StringInfoData buf;
// 	NameData oldCurestatus;

// 	namestrcpy(&oldCurestatus, NameStr(mgrNode->form.curestatus));
// 	namestrcpy(&mgrNode->form.curestatus, CURE_STATUS_NORMAL);
// 	mgrNode->form.nodeinited = true;
// 	mgrNode->form.nodeincluster = true;

// 	initStringInfo(&buf);
// 	appendStringInfo(&buf,
// 					 "update pg_catalog.mgr_node "
// 					 "set curestatus = '%s', "
// 					 "nodesync = '%s', "
// 					 "nodeinited = %d::boolean, "
// 					 "nodeincluster = %d::boolean "
// 					 "WHERE oid = %u "
// 					 "and curestatus = '%s' "
// 					 "and nodetype = '%c' ",
// 					 NameStr(mgrNode->form.curestatus),
// 					 NameStr(mgrNode->form.nodesync),
// 					 mgrNode->form.nodeinited,
// 					 mgrNode->form.nodeincluster,
// 					 mgrNode->oid,
// 					 NameStr(oldCurestatus),
// 					 mgrNode->form.nodetype);
// 	if (executeUpdateSql(buf.data, spiContext) != 1)
// 	{
// 		ereport(ERROR,
// 				(errmsg("%s, can not transit to curestatus:%s",
// 						NameStr(mgrNode->form.nodename),
// 						NameStr(mgrNode->form.curestatus))));
// 	}
// }

static void selectSiblingActiveNodes(MgrNodeWrapper *faultNode,
									 dlist_head *resultList,
									 MemoryContext spiContext)
{
	StringInfoData sql;

	initStringInfo(&sql);
	appendStringInfo(&sql,
					 "SELECT * \n"
					 "FROM pg_catalog.mgr_node \n"
					 "WHERE nodetype in ('%c') \n"
					 "AND nodeinited = %d::boolean \n"
					 "AND nodeincluster = %d::boolean \n"
					 "AND nodemasternameoid = %u \n"
					 "AND curestatus != '%s' \n"
					 "AND nodename != '%s' \n",
					 faultNode->form.nodetype,
					 true,
					 true,
					 faultNode->form.nodemasternameoid,
					 CURE_STATUS_ISOLATED,
					 NameStr(faultNode->form.nodename));
	selectMgrNodes(sql.data, spiContext, resultList);
	pfree(sql.data);
}

static void updateMgrNodeNodesync(MgrNodeWrapper *mgrNode,
								  MemoryContext spiContext)
{
	StringInfoData buf;
	int spiRes;
	uint64 rows;
	MemoryContext oldCtx;

	initStringInfo(&buf);
	appendStringInfo(&buf,
					 "update pg_catalog.mgr_node  \n"
					 "set nodesync = '%s' \n"
					 "WHERE oid = %u \n"
					 "and nodemasternameoid = %u \n"
					 "and curestatus = '%s' \n"
					 "and nodetype = '%c' \n",
					 NameStr(mgrNode->form.nodesync),
					 mgrNode->oid,
					 mgrNode->form.nodemasternameoid,
					 NameStr(mgrNode->form.curestatus),
					 mgrNode->form.nodetype);
	oldCtx = MemoryContextSwitchTo(spiContext);
	spiRes = SPI_execute(buf.data, false, 0);
	MemoryContextSwitchTo(oldCtx);
	pfree(buf.data);
	if (spiRes != SPI_OK_UPDATE)
		ereport(ERROR,
				(errmsg("SPI_execute failed: error code %d",
						spiRes)));
	rows = SPI_processed;
	if (rows != 1)
	{
		ereport(ERROR,
				(errmsg("%s, update nodesync to %s failed",
						NameStr(mgrNode->form.nodename),
						NameStr(mgrNode->form.nodesync))));
	}
}

static bool checkGetMasterNode(MgrNodeWrapper *faultSlaveNode,
							   SwitcherNodeWrapper **masterNodeP)
{
	MgrNodeWrapper *mgrNode;
	SwitcherNodeWrapper *masterNode;
	int ret;
	MemoryContext oldContext;
	MemoryContext spiContext;

	oldContext = CurrentMemoryContext;
	SPI_CONNECT_TRANSACTIONAL_START(ret, true);
	spiContext = CurrentMemoryContext;
	MemoryContextSwitchTo(oldContext);

	mgrNode = selectMgrNodeByOid(faultSlaveNode->form.nodemasternameoid, spiContext);
	SPI_FINISH_TRANSACTIONAL_COMMIT();

	masterNode = palloc0(sizeof(SwitcherNodeWrapper));
	*masterNodeP = masterNode;
	masterNode->mgrNode = mgrNode;

	if (!mgrNode)
	{
		ereport(LOG,
				(errmsg("master node does not exist")));
		return false;
	}
	if (mgrNode->form.nodetype != getMgrMasterNodetype(faultSlaveNode->form.nodetype))
	{
		ereport(LOG,
				(errmsg("%s is not a master node",
						NameStr(mgrNode->form.nodename))));
		return false;
	}
	if (!mgrNode->form.nodeinited)
	{
		ereport(LOG,
				(errmsg("%s has not be initialized",
						NameStr(mgrNode->form.nodename))));
		return false;
	}
	if (!mgrNode->form.nodeincluster)
	{
		ereport(LOG,
				(errmsg("%s has been kicked out of the cluster",
						NameStr(mgrNode->form.nodename))));
		return false;
	}

	masterNode->pgConn = getNodeDefaultDBConnection(masterNode->mgrNode, 10);
	if (masterNode->pgConn == NULL)
	{
		ereport(LOG,
				(errmsg("%s connection failed",
						NameStr(mgrNode->form.nodename))));
		return false;
	}
	else
	{
		masterNode->runningMode = getNodeRunningMode(masterNode->pgConn);
		if (masterNode->runningMode != NODE_RUNNING_MODE_MASTER)
		{
			ereport(LOG,
					(errmsg("%s configured as master, "
							"but actually did not running on that status",
							NameStr(masterNode->mgrNode->form.nodename))));
			return false;
		}
	}
	return true;
}

static RepairerConfiguration *newRepairerConfiguration(AdbDoctorConf *conf)
{
	RepairerConfiguration *rc;

	checkAdbDoctorConf(conf);

	rc = palloc0(sizeof(RepairerConfiguration));

	rc->repairIntervalMs = conf->retry_repair_interval_ms;
	ereport(DEBUG1,
			(errmsg("%s configuration: "
					"repairIntervalMs:%ld",
					MyBgworkerEntry->bgw_name,
					rc->repairIntervalMs)));
	return rc;
}

static void examineAdbDoctorConf(dlist_head *mgrNodes)
{
	AdbDoctorConf *confInLocal;
	dlist_head freshMgrNodes = DLIST_STATIC_INIT(freshMgrNodes);
	dlist_head staleMgrNodes = DLIST_STATIC_INIT(staleMgrNodes);
	if (gotSigusr1)
	{
		gotSigusr1 = false;

		confInLocal = copyAdbDoctorConfFromShm(confShm);
		pfree(repairerConfiguration);
		repairerConfiguration = newRepairerConfiguration(confInLocal);
		pfree(confInLocal);

		ereport(LOG,
				(errmsg("%s, Refresh configuration completed",
						MyBgworkerEntry->bgw_name)));

		getCheckMgrNodesForRepairer(&freshMgrNodes);
		if (isIdenticalDoctorMgrNodes(&freshMgrNodes, &staleMgrNodes))
		{
			pfreeMgrNodeWrapperList(&freshMgrNodes, NULL);
		}
		else
		{
			pfreeMgrNodeWrapperList(&freshMgrNodes, NULL);
			resetRepairer();
		}
	}
}

static void resetRepairer()
{
	ereport(LOG,
			(errmsg("%s, reset repairer",
					MyBgworkerEntry->bgw_name)));
	siglongjmp(reset_repairer_sigjmp_buf, 1);
}

/*
 * When we receive a SIGTERM, we set InterruptPending and ProcDiePending just
 * like a normal backend.  The next CHECK_FOR_INTERRUPTS() will do the right
 * thing.
 */
static void handleSigterm(SIGNAL_ARGS)
{
	int save_errno = errno;

	gotSigterm = true;

	SetLatch(MyLatch);

	if (!proc_exit_inprogress)
	{
		InterruptPending = true;
		ProcDiePending = true;
	}
	errno = save_errno;
}

/*
 * When we receive a SIGUSR1, we set gotSigusr1 = true
 */
static void handleSigusr1(SIGNAL_ARGS)
{
	int save_errno = errno;

	gotSigusr1 = true;

	procsignal_sigusr1_handler(postgres_signal_arg);

	errno = save_errno;
}