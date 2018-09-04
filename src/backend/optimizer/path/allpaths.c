/*-------------------------------------------------------------------------
 *
 * allpaths.c
 *	  Routines to find possible search paths for processing a query
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/path/allpaths.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <limits.h>
#include <math.h>

#include "access/sysattr.h"
#include "access/tsmapi.h"
#include "catalog/pg_class.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "foreign/fdwapi.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#ifdef OPTIMIZER_DEBUG
#include "nodes/print.h"
#endif
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/geqo.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "optimizer/planner.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/tlist.h"
#include "optimizer/var.h"
#include "parser/parse_clause.h"
#include "parser/parsetree.h"
#include "rewrite/rewriteManip.h"
#include "utils/lsyscache.h"
#ifdef ADB
#include "catalog/pgxc_node.h"
#include "optimizer/planmain.h"
#include "optimizer/reduceinfo.h"
#include "pgxc/pgxcnode.h"
#endif /* ADB */

/* results of subquery_is_pushdown_safe */
typedef struct pushdown_safety_info
{
	bool	   *unsafeColumns;	/* which output columns are unsafe to use */
	bool		unsafeVolatile; /* don't push down volatile quals */
	bool		unsafeLeaky;	/* don't push down leaky quals */
} pushdown_safety_info;

/* These parameters are set by GUC */
bool		enable_geqo = false;	/* just in case GUC doesn't set it */
int			geqo_threshold;
int			min_parallel_table_scan_size;
int			min_parallel_index_scan_size;

/* Hook for plugins to get control in set_rel_pathlist() */
set_rel_pathlist_hook_type set_rel_pathlist_hook = NULL;

/* Hook for plugins to replace standard_join_search() */
join_search_hook_type join_search_hook = NULL;


static void set_base_rel_consider_startup(PlannerInfo *root);
static void set_base_rel_sizes(PlannerInfo *root);
static void set_base_rel_pathlists(PlannerInfo *root);
static void set_rel_size(PlannerInfo *root, RelOptInfo *rel,
			 Index rti, RangeTblEntry *rte);
static void set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
				 Index rti, RangeTblEntry *rte);
static void set_plain_rel_size(PlannerInfo *root, RelOptInfo *rel,
				   RangeTblEntry *rte);
static void create_plain_partial_paths(PlannerInfo *root, RelOptInfo *rel);
static void set_rel_consider_parallel(PlannerInfo *root, RelOptInfo *rel,
						  RangeTblEntry *rte);
static void set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
					   RangeTblEntry *rte);
static void set_tablesample_rel_size(PlannerInfo *root, RelOptInfo *rel,
						 RangeTblEntry *rte);
static void set_tablesample_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
							 RangeTblEntry *rte);
static void set_foreign_size(PlannerInfo *root, RelOptInfo *rel,
				 RangeTblEntry *rte);
static void set_foreign_pathlist(PlannerInfo *root, RelOptInfo *rel,
					 RangeTblEntry *rte);
static void set_append_rel_size(PlannerInfo *root, RelOptInfo *rel,
					Index rti, RangeTblEntry *rte);
static void set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
						Index rti, RangeTblEntry *rte);
static void generate_mergeappend_paths(PlannerInfo *root, RelOptInfo *rel,
						   List *live_childrels,
						   List *all_child_pathkeys,
						   List *partitioned_rels);
static Path *get_cheapest_parameterized_child_path(PlannerInfo *root,
									  RelOptInfo *rel,
									  Relids required_outer);
static List *accumulate_append_subpath(List *subpaths, Path *path);
static void set_subquery_pathlist(PlannerInfo *root, RelOptInfo *rel,
					  Index rti, RangeTblEntry *rte);
static void set_function_pathlist(PlannerInfo *root, RelOptInfo *rel,
					  RangeTblEntry *rte);
static void set_values_pathlist(PlannerInfo *root, RelOptInfo *rel,
					RangeTblEntry *rte);
static void set_tablefunc_pathlist(PlannerInfo *root, RelOptInfo *rel,
					   RangeTblEntry *rte);
static void set_cte_pathlist(PlannerInfo *root, RelOptInfo *rel,
				 RangeTblEntry *rte);
static void set_namedtuplestore_pathlist(PlannerInfo *root, RelOptInfo *rel,
							 RangeTblEntry *rte);
#ifdef ADB
static void set_paramtuplestore_pathlist(PlannerInfo *root, RelOptInfo *rel,
							 RangeTblEntry *rte);
#endif /* ADB */
static void set_worktable_pathlist(PlannerInfo *root, RelOptInfo *rel,
					   RangeTblEntry *rte);
static RelOptInfo *make_rel_from_joinlist(PlannerInfo *root, List *joinlist);
static bool subquery_is_pushdown_safe(Query *subquery, Query *topquery,
						  pushdown_safety_info *safetyInfo);
static bool recurse_pushdown_safe(Node *setOp, Query *topquery,
					  pushdown_safety_info *safetyInfo);
static void check_output_expressions(Query *subquery,
						 pushdown_safety_info *safetyInfo);
static void compare_tlist_datatypes(List *tlist, List *colTypes,
						pushdown_safety_info *safetyInfo);
static bool targetIsInAllPartitionLists(TargetEntry *tle, Query *query);
static bool qual_is_pushdown_safe(Query *subquery, Index rti, Node *qual,
					  pushdown_safety_info *safetyInfo);
static void subquery_push_qual(Query *subquery,
				   RangeTblEntry *rte, Index rti, Node *qual);
static void recurse_push_qual(Node *setOp, Query *topquery,
				  RangeTblEntry *rte, Index rti, Node *qual);
static void remove_unused_subquery_outputs(Query *subquery, RelOptInfo *rel);
static void add_paths_to_append_rel(PlannerInfo *root, RelOptInfo *rel,
						List *live_childrels);

#ifdef ADB
static bool set_path_reduce_info_worker(Path *path, List *reduce_info_list);
static bool get_subplan_ref_walker(Expr *expr, int *subplan_ref);
static int get_max_parallel_workers(List *pathlist);
#endif /* ADB */

/*
 * make_one_rel
 *	  Finds all possible access paths for executing a query, returning a
 *	  single rel that represents the join of all base rels in the query.
 */
RelOptInfo *
make_one_rel(PlannerInfo *root, List *joinlist)
{
	RelOptInfo *rel;
	Index		rti;

	/*
	 * Construct the all_baserels Relids set.
	 */
	root->all_baserels = NULL;
	for (rti = 1; rti < root->simple_rel_array_size; rti++)
	{
		RelOptInfo *brel = root->simple_rel_array[rti];

		/* there may be empty slots corresponding to non-baserel RTEs */
		if (brel == NULL)
			continue;

		Assert(brel->relid == rti); /* sanity check on array */

		/* ignore RTEs that are "other rels" */
		if (brel->reloptkind != RELOPT_BASEREL)
			continue;

		root->all_baserels = bms_add_member(root->all_baserels, brel->relid);
	}

	/* Mark base rels as to whether we care about fast-start plans */
	set_base_rel_consider_startup(root);

	/*
	 * Compute size estimates and consider_parallel flags for each base rel,
	 * then generate access paths.
	 */
	set_base_rel_sizes(root);
	set_base_rel_pathlists(root);

	/*
	 * Generate access paths for the entire join tree.
	 */
	rel = make_rel_from_joinlist(root, joinlist);

	/*
	 * The result should join all and only the query's base rels.
	 */
	Assert(bms_equal(rel->relids, root->all_baserels));

	return rel;
}

/*
 * set_base_rel_consider_startup
 *	  Set the consider_[param_]startup flags for each base-relation entry.
 *
 * For the moment, we only deal with consider_param_startup here; because the
 * logic for consider_startup is pretty trivial and is the same for every base
 * relation, we just let build_simple_rel() initialize that flag correctly to
 * start with.  If that logic ever gets more complicated it would probably
 * be better to move it here.
 */
static void
set_base_rel_consider_startup(PlannerInfo *root)
{
	/*
	 * Since parameterized paths can only be used on the inside of a nestloop
	 * join plan, there is usually little value in considering fast-start
	 * plans for them.  However, for relations that are on the RHS of a SEMI
	 * or ANTI join, a fast-start plan can be useful because we're only going
	 * to care about fetching one tuple anyway.
	 *
	 * To minimize growth of planning time, we currently restrict this to
	 * cases where the RHS is a single base relation, not a join; there is no
	 * provision for consider_param_startup to get set at all on joinrels.
	 * Also we don't worry about appendrels.  costsize.c's costing rules for
	 * nestloop semi/antijoins don't consider such cases either.
	 */
	ListCell   *lc;

	foreach(lc, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(lc);
		int			varno;

		if ((sjinfo->jointype == JOIN_SEMI || sjinfo->jointype == JOIN_ANTI) &&
			bms_get_singleton_member(sjinfo->syn_righthand, &varno))
		{
			RelOptInfo *rel = find_base_rel(root, varno);

			rel->consider_param_startup = true;
		}
	}
}

/*
 * set_base_rel_sizes
 *	  Set the size estimates (rows and widths) for each base-relation entry.
 *	  Also determine whether to consider parallel paths for base relations.
 *
 * We do this in a separate pass over the base rels so that rowcount
 * estimates are available for parameterized path generation, and also so
 * that each rel's consider_parallel flag is set correctly before we begin to
 * generate paths.
 */
static void
set_base_rel_sizes(PlannerInfo *root)
{
	Index		rti;

	for (rti = 1; rti < root->simple_rel_array_size; rti++)
	{
		RelOptInfo *rel = root->simple_rel_array[rti];
		RangeTblEntry *rte;

		/* there may be empty slots corresponding to non-baserel RTEs */
		if (rel == NULL)
			continue;

		Assert(rel->relid == rti);	/* sanity check on array */

		/* ignore RTEs that are "other rels" */
		if (rel->reloptkind != RELOPT_BASEREL)
			continue;

		rte = root->simple_rte_array[rti];

		/*
		 * If parallelism is allowable for this query in general, see whether
		 * it's allowable for this rel in particular.  We have to do this
		 * before set_rel_size(), because (a) if this rel is an inheritance
		 * parent, set_append_rel_size() will use and perhaps change the rel's
		 * consider_parallel flag, and (b) for some RTE types, set_rel_size()
		 * goes ahead and makes paths immediately.
		 */
		if (root->glob->parallelModeOK)
			set_rel_consider_parallel(root, rel, rte);

		set_rel_size(root, rel, rti, rte);
	}
}

/*
 * set_base_rel_pathlists
 *	  Finds all paths available for scanning each base-relation entry.
 *	  Sequential scan and any available indices are considered.
 *	  Each useful path is attached to its relation's 'pathlist' field.
 */
static void
set_base_rel_pathlists(PlannerInfo *root)
{
	Index		rti;

	for (rti = 1; rti < root->simple_rel_array_size; rti++)
	{
		RelOptInfo *rel = root->simple_rel_array[rti];

		/* there may be empty slots corresponding to non-baserel RTEs */
		if (rel == NULL)
			continue;

		Assert(rel->relid == rti);	/* sanity check on array */

		/* ignore RTEs that are "other rels" */
		if (rel->reloptkind != RELOPT_BASEREL)
			continue;

		set_rel_pathlist(root, rel, rti, root->simple_rte_array[rti]);
	}
}

/*
 * set_rel_size
 *	  Set size estimates for a base relation
 */
static void
set_rel_size(PlannerInfo *root, RelOptInfo *rel,
			 Index rti, RangeTblEntry *rte)
{
	if (rel->reloptkind == RELOPT_BASEREL &&
		relation_excluded_by_constraints(root, rel, rte))
	{
		/*
		 * We proved we don't need to scan the rel via constraint exclusion,
		 * so set up a single dummy path for it.  Here we only check this for
		 * regular baserels; if it's an otherrel, CE was already checked in
		 * set_append_rel_size().
		 *
		 * In this case, we go ahead and set up the relation's path right away
		 * instead of leaving it for set_rel_pathlist to do.  This is because
		 * we don't have a convention for marking a rel as dummy except by
		 * assigning a dummy path to it.
		 */
		set_dummy_rel_pathlist(rel);
	}
	else if (rte->inh)
	{
		/* It's an "append relation", process accordingly */
		set_append_rel_size(root, rel, rti, rte);
	}
	else
	{
		switch (rel->rtekind)
		{
			case RTE_RELATION:
				if (rte->relkind == RELKIND_FOREIGN_TABLE)
				{
					/* Foreign table */
					set_foreign_size(root, rel, rte);
				}
				else if (rte->relkind == RELKIND_PARTITIONED_TABLE)
				{
					/*
					 * A partitioned table without leaf partitions is marked
					 * as a dummy rel.
					 */
					set_dummy_rel_pathlist(rel);
				}
				else if (rte->tablesample != NULL)
				{
					/* Sampled relation */
					set_tablesample_rel_size(root, rel, rte);
				}
				else
				{
					/* Plain relation */
					set_plain_rel_size(root, rel, rte);
				}
				break;
			case RTE_SUBQUERY:

				/*
				 * Subqueries don't support making a choice between
				 * parameterized and unparameterized paths, so just go ahead
				 * and build their paths immediately.
				 */
				set_subquery_pathlist(root, rel, rti, rte);
				break;
			case RTE_FUNCTION:
				set_function_size_estimates(root, rel);
				break;
			case RTE_TABLEFUNC:
				set_tablefunc_size_estimates(root, rel);
				break;
			case RTE_VALUES:
				set_values_size_estimates(root, rel);
				break;
			case RTE_CTE:

				/*
				 * CTEs don't support making a choice between parameterized
				 * and unparameterized paths, so just go ahead and build their
				 * paths immediately.
				 */
				if (rte->self_reference)
					set_worktable_pathlist(root, rel, rte);
				else
					set_cte_pathlist(root, rel, rte);
				break;
			case RTE_NAMEDTUPLESTORE:
				set_namedtuplestore_pathlist(root, rel, rte);
				break;
#ifdef ADB
			case RTE_PARAMTS:
				set_paramtuplestore_pathlist(root, rel, rte);
				break;
#endif /* ADB */
			default:
				elog(ERROR, "unexpected rtekind: %d", (int) rel->rtekind);
				break;
		}
	}

	/*
	 * We insist that all non-dummy rels have a nonzero rowcount estimate.
	 */
	Assert(rel->rows > 0 || IS_DUMMY_REL(rel));
}

/*
 * set_rel_pathlist
 *	  Build access paths for a base relation
 */
static void
set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
				 Index rti, RangeTblEntry *rte)
{
	if (IS_DUMMY_REL(rel))
	{
		/* We already proved the relation empty, so nothing more to do */
	}
	else if (rte->inh)
	{
		/* It's an "append relation", process accordingly */
		set_append_rel_pathlist(root, rel, rti, rte);
	}
	else
	{
		switch (rel->rtekind)
		{
			case RTE_RELATION:
				if (rte->relkind == RELKIND_FOREIGN_TABLE)
				{
					/* Foreign table */
					set_foreign_pathlist(root, rel, rte);
				}
				else if (rte->tablesample != NULL)
				{
					/* Sampled relation */
					set_tablesample_rel_pathlist(root, rel, rte);
				}
				else
				{
					/* Plain relation */
					set_plain_rel_pathlist(root, rel, rte);
				}
				break;
			case RTE_SUBQUERY:
				/* Subquery --- fully handled during set_rel_size */
				break;
			case RTE_FUNCTION:
				/* RangeFunction */
				set_function_pathlist(root, rel, rte);
				break;
			case RTE_TABLEFUNC:
				/* Table Function */
				set_tablefunc_pathlist(root, rel, rte);
				break;
			case RTE_VALUES:
				/* Values list */
				set_values_pathlist(root, rel, rte);
				break;
			case RTE_CTE:
				/* CTE reference --- fully handled during set_rel_size */
				break;
			case RTE_NAMEDTUPLESTORE:
				/* tuplestore reference --- fully handled during set_rel_size */
				break;
#ifdef ADB
			case RTE_PARAMTS:
				/* tuplestore reference --- fully handled during set_rel_size */
				break;
#endif /* ADB */
			default:
				elog(ERROR, "unexpected rtekind: %d", (int) rel->rtekind);
				break;
		}
	}

	/*
	 * If this is a baserel, consider gathering any partial paths we may have
	 * created for it.  (If we tried to gather inheritance children, we could
	 * end up with a very large number of gather nodes, each trying to grab
	 * its own pool of workers, so don't do this for otherrels.  Instead,
	 * we'll consider gathering partial paths for the parent appendrel.)
	 */
	if (rel->reloptkind == RELOPT_BASEREL)
		generate_gather_paths(root, rel);

	/*
	 * Allow a plugin to editorialize on the set of Paths for this base
	 * relation.  It could add new paths (such as CustomPaths) by calling
	 * add_path(), or delete or modify paths added by the core code.
	 */
	if (set_rel_pathlist_hook)
		(*set_rel_pathlist_hook) (root, rel, rti, rte);

	/* Now find the cheapest of the paths for this rel */
	set_cheapest(rel);

#ifdef OPTIMIZER_DEBUG
	debug_print_rel(root, rel);
#endif
}

/*
 * set_plain_rel_size
 *	  Set size estimates for a plain relation (no subquery, no inheritance)
 */
static void
set_plain_rel_size(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	/*
	 * Test any partial indexes of rel for applicability.  We must do this
	 * first since partial unique indexes can affect size estimates.
	 */
	check_index_predicates(root, rel);

#ifdef ADB
	if (rel->loc_info)
		rel->remote_oids = relation_remote_by_constraints(root, rel, use_aux_type >= USE_AUX_CTID);
#endif /* ADB */

	/* Mark rel with estimated output rows, width, etc */
	set_baserel_size_estimates(root, rel);
}

/*
 * If this relation could possibly be scanned from within a worker, then set
 * its consider_parallel flag.
 */
static void
set_rel_consider_parallel(PlannerInfo *root, RelOptInfo *rel,
						  RangeTblEntry *rte)
{
	/*
	 * The flag has previously been initialized to false, so we can just
	 * return if it becomes clear that we can't safely set it.
	 */
	Assert(!rel->consider_parallel);

	/* Don't call this if parallelism is disallowed for the entire query. */
	Assert(root->glob->parallelModeOK);

	/* This should only be called for baserels and appendrel children. */
	Assert(IS_SIMPLE_REL(rel));

	/* Assorted checks based on rtekind. */
	switch (rte->rtekind)
	{
		case RTE_RELATION:

			/*
			 * Currently, parallel workers can't access the leader's temporary
			 * tables.  We could possibly relax this if the wrote all of its
			 * local buffers at the start of the query and made no changes
			 * thereafter (maybe we could allow hint bit changes), and if we
			 * taught the workers to read them.  Writing a large number of
			 * temporary buffers could be expensive, though, and we don't have
			 * the rest of the necessary infrastructure right now anyway.  So
			 * for now, bail out if we see a temporary table.
			 */
			if (get_rel_persistence(rte->relid) == RELPERSISTENCE_TEMP)
				return;

			/*
			 * Table sampling can be pushed down to workers if the sample
			 * function and its arguments are safe.
			 */
			if (rte->tablesample != NULL)
			{
				char		proparallel = func_parallel(rte->tablesample->tsmhandler);

				if (proparallel != PROPARALLEL_SAFE)
					return;
				if (!is_parallel_safe(root, (Node *) rte->tablesample->args))
					return;
			}

			/*
			 * Ask FDWs whether they can support performing a ForeignScan
			 * within a worker.  Most often, the answer will be no.  For
			 * example, if the nature of the FDW is such that it opens a TCP
			 * connection with a remote server, each parallel worker would end
			 * up with a separate connection, and these connections might not
			 * be appropriately coordinated between workers and the leader.
			 */
			if (rte->relkind == RELKIND_FOREIGN_TABLE)
			{
				Assert(rel->fdwroutine);
				if (!rel->fdwroutine->IsForeignScanParallelSafe)
					return;
				if (!rel->fdwroutine->IsForeignScanParallelSafe(root, rel, rte))
					return;
			}

			/*
			 * There are additional considerations for appendrels, which we'll
			 * deal with in set_append_rel_size and set_append_rel_pathlist.
			 * For now, just set consider_parallel based on the rel's own
			 * quals and targetlist.
			 */
			break;

		case RTE_SUBQUERY:

			/*
			 * There's no intrinsic problem with scanning a subquery-in-FROM
			 * (as distinct from a SubPlan or InitPlan) in a parallel worker.
			 * If the subquery doesn't happen to have any parallel-safe paths,
			 * then flagging it as consider_parallel won't change anything,
			 * but that's true for plain tables, too.  We must set
			 * consider_parallel based on the rel's own quals and targetlist,
			 * so that if a subquery path is parallel-safe but the quals and
			 * projection we're sticking onto it are not, we correctly mark
			 * the SubqueryScanPath as not parallel-safe.  (Note that
			 * set_subquery_pathlist() might push some of these quals down
			 * into the subquery itself, but that doesn't change anything.)
			 */
			break;

		case RTE_JOIN:
			/* Shouldn't happen; we're only considering baserels here. */
			Assert(false);
			return;

		case RTE_FUNCTION:
			/* Check for parallel-restricted functions. */
			if (!is_parallel_safe(root, (Node *) rte->functions))
				return;
			break;

		case RTE_TABLEFUNC:
			/* not parallel safe */
			return;

		case RTE_VALUES:
			/* Check for parallel-restricted functions. */
			if (!is_parallel_safe(root, (Node *) rte->values_lists))
				return;
			break;

		case RTE_CTE:
#ifdef ADB
		case RTE_PARAMTS:
		/*
		 * ADBQ: Can not decide remote dummy work with parallel now.
		 */
		case RTE_REMOTE_DUMMY:
#endif

			/*
			 * CTE tuplestores aren't shared among parallel workers, so we
			 * force all CTE scans to happen in the leader.  Also, populating
			 * the CTE would require executing a subplan that's not available
			 * in the worker, might be parallel-restricted, and must get
			 * executed only once.
			 */
			return;

		case RTE_NAMEDTUPLESTORE:

			/*
			 * tuplestore cannot be shared, at least without more
			 * infrastructure to support that.
			 */
			return;
	}

	/*
	 * If there's anything in baserestrictinfo that's parallel-restricted, we
	 * give up on parallelizing access to this relation.  We could consider
	 * instead postponing application of the restricted quals until we're
	 * above all the parallelism in the plan tree, but it's not clear that
	 * that would be a win in very many cases, and it might be tricky to make
	 * outer join clauses work correctly.  It would likely break equivalence
	 * classes, too.
	 */
	if (!is_parallel_safe(root, (Node *) rel->baserestrictinfo))
		return;

	/*
	 * Likewise, if the relation's outputs are not parallel-safe, give up.
	 * (Usually, they're just Vars, but sometimes they're not.)
	 */
	if (!is_parallel_safe(root, (Node *) rel->reltarget->exprs))
		return;

	/* We have a winner. */
	rel->consider_parallel = true;
}

/*
 * set_plain_rel_pathlist
 *	  Build access paths for a plain relation (no subquery, no inheritance)
 */
static void
set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;

	/*
	 * We don't support pushing join clauses into the quals of a seqscan, but
	 * it could still have required parameterization due to LATERAL refs in
	 * its tlist.
	 */
	required_outer = rel->lateral_relids;
#ifdef ADB
	if (rel->loc_info &&
		rel->remote_oids == NIL)
	{
		set_dummy_rel_pathlist(rel);
		return;
	}

	if(root->glob->clusterPlanOK && rel->loc_info && (LOCATOR_TYPE_META!=rel->loc_info->locatorType))
	{
		Path *path;
		ListCell *lc;
		List *reduce_info_list;
		ReduceInfo *rinfo;
		List *save_clauses;
		List *exec_param_clauses;
		List *base_clauses;
		List *exclude = NIL;
		RelationLocInfo *loc_info = rel->loc_info;
		if (IsLocatorDistributedByValue(loc_info->locatorType) ||
			loc_info->locatorType == LOCATOR_TYPE_USER_DEFINED)
		{
			exclude = list_difference_oid(rel->loc_info->nodeids, rel->remote_oids);
		}
		rinfo = MakeReduceInfoFromLocInfo(loc_info, exclude, rte->relid, rel->relid);
		list_free(exclude);

		exec_param_clauses = NIL;
		save_clauses = rel->baserestrictinfo;
		if(root->must_replicate)
		{
			foreach(lc, save_clauses)
			{
				RestrictInfo *ri = lfirst(lc);
				if(expression_have_exec_param(ri->clause))
				{
					exec_param_clauses = lappend(exec_param_clauses, ri);
				}
			}
			if(exec_param_clauses)
			{
				base_clauses = list_difference_ptr(save_clauses, exec_param_clauses);
				rel->baserestrictinfo = base_clauses;
			}
		}

		add_path(rel, create_seqscan_path(root, rel, required_outer, 0));

		/* Consider index scans */
		create_index_paths(root, rel);

		/* Consider TID scans */
		create_tidscan_paths(root, rel);

		reduce_info_list = list_make1(rinfo);

		/* recost pathlist */
		foreach(lc, rel->pathlist)
		{
			path = lfirst(lc);

			set_path_reduce_info_worker(path, reduce_info_list);

			cost_div(path, list_length(loc_info->nodeids));
		}

		if (exec_param_clauses)
		{
			List *replicate = list_make1(MakeFinalReplicateReduceInfo());

			foreach(lc, rel->pathlist)
			{
				path = lfirst(lc);
				path = (Path*)try_reducescan_path(root, rel, path->pathtarget, lfirst(lc), replicate, path->pathkeys, exec_param_clauses);
				if(path)
				{
					/* just using lappend, don't need add_cluster_path(...) */
					rel->cluster_pathlist = lappend(rel->cluster_pathlist, path);
				}
			}
			if(rel->cluster_pathlist == NIL)
			{
				path = create_seqscan_path(root, rel, required_outer, 0);
				set_path_reduce_info_worker(path, reduce_info_list);
				cost_div(path, list_length(loc_info->nodeids));
				path = (Path*)try_reducescan_path(root, rel, path->pathtarget, path, replicate, NULL, exec_param_clauses);
				Assert(path);
				rel->cluster_pathlist = list_make1(path);
			}
			rel->baserestrictinfo = save_clauses;
		}else
		{
			/* move pathlist to cluster_pathlist */
			rel->cluster_pathlist = rel->pathlist;

			/* If appropriate, consider parallel sequential scan */
			if (rel->consider_parallel && required_outer == NULL)
			{
				create_plain_partial_paths(root, rel);
				/* move pathlist to cluster_partial_pathlist */
				foreach(lc, rel->partial_pathlist)
				{
					path = lfirst(lc);

					set_path_reduce_info_worker(path, reduce_info_list);

					cost_div(path, list_length(loc_info->nodeids));
					add_cluster_partial_path(rel, path);
				}
				rel->partial_pathlist = NIL;
			}
		}
		rel->pathlist = NIL;
	}
	if (!create_plainrel_rqpath(root, rel, rte, required_outer))
	{
#endif
	/* Consider sequential scan */
	add_path(rel, create_seqscan_path(root, rel, required_outer, 0));

	/* If appropriate, consider parallel sequential scan */
	if (rel->consider_parallel && required_outer == NULL)
		create_plain_partial_paths(root, rel);

	/* Consider index scans */
	create_index_paths(root, rel);

	/* Consider TID scans */
	create_tidscan_paths(root, rel);
#ifdef ADB
	}
#endif
}

/*
 * create_plain_partial_paths
 *	  Build partial access paths for parallel scan of a plain relation
 */
static void
create_plain_partial_paths(PlannerInfo *root, RelOptInfo *rel)
{
	int			parallel_workers;

	parallel_workers = compute_parallel_worker(rel, rel->pages, -1);

	/* If any limit was set to zero, the user doesn't want a parallel scan. */
	if (parallel_workers <= 0)
		return;

	/* Add an unordered partial path based on a parallel sequential scan. */
	add_partial_path(rel, create_seqscan_path(root, rel, NULL, parallel_workers));
}

/*
 * set_tablesample_rel_size
 *	  Set size estimates for a sampled relation
 */
static void
set_tablesample_rel_size(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	TableSampleClause *tsc = rte->tablesample;
	TsmRoutine *tsm;
	BlockNumber pages;
	double		tuples;

	/*
	 * Test any partial indexes of rel for applicability.  We must do this
	 * first since partial unique indexes can affect size estimates.
	 */
	check_index_predicates(root, rel);

	/*
	 * Call the sampling method's estimation function to estimate the number
	 * of pages it will read and the number of tuples it will return.  (Note:
	 * we assume the function returns sane values.)
	 */
	tsm = GetTsmRoutine(tsc->tsmhandler);
	tsm->SampleScanGetSampleSize(root, rel, tsc->args,
								 &pages, &tuples);

	/*
	 * For the moment, because we will only consider a SampleScan path for the
	 * rel, it's okay to just overwrite the pages and tuples estimates for the
	 * whole relation.  If we ever consider multiple path types for sampled
	 * rels, we'll need more complication.
	 */
	rel->pages = pages;
	rel->tuples = tuples;

	/* Mark rel with estimated output rows, width, etc */
	set_baserel_size_estimates(root, rel);
}

/*
 * set_tablesample_rel_pathlist
 *	  Build access paths for a sampled relation
 */
static void
set_tablesample_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;
	Path	   *path;

	/*
	 * We don't support pushing join clauses into the quals of a samplescan,
	 * but it could still have required parameterization due to LATERAL refs
	 * in its tlist or TABLESAMPLE arguments.
	 */
	required_outer = rel->lateral_relids;

	/* Consider sampled scan */
	path = create_samplescan_path(root, rel, required_outer);

	/*
	 * If the sampling method does not support repeatable scans, we must avoid
	 * plans that would scan the rel multiple times.  Ideally, we'd simply
	 * avoid putting the rel on the inside of a nestloop join; but adding such
	 * a consideration to the planner seems like a great deal of complication
	 * to support an uncommon usage of second-rate sampling methods.  Instead,
	 * if there is a risk that the query might perform an unsafe join, just
	 * wrap the SampleScan in a Materialize node.  We can check for joins by
	 * counting the membership of all_baserels (note that this correctly
	 * counts inheritance trees as single rels).  If we're inside a subquery,
	 * we can't easily check whether a join might occur in the outer query, so
	 * just assume one is possible.
	 *
	 * GetTsmRoutine is relatively expensive compared to the other tests here,
	 * so check repeatable_across_scans last, even though that's a bit odd.
	 */
	if ((root->query_level > 1 ||
		 bms_membership(root->all_baserels) != BMS_SINGLETON) &&
		!(GetTsmRoutine(rte->tablesample->tsmhandler)->repeatable_across_scans))
	{
		path = (Path *) create_material_path(rel, path);
	}

	add_path(rel, path);

	/* For the moment, at least, there are no other paths to consider */
}

/*
 * set_foreign_size
 *		Set size estimates for a foreign table RTE
 */
static void
set_foreign_size(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	/* Mark rel with estimated output rows, width, etc */
	set_foreign_size_estimates(root, rel);

	/* Let FDW adjust the size estimates, if it can */
	rel->fdwroutine->GetForeignRelSize(root, rel, rte->relid);

	/* ... but do not let it set the rows estimate to zero */
	rel->rows = clamp_row_est(rel->rows);
}

/*
 * set_foreign_pathlist
 *		Build access paths for a foreign table RTE
 */
static void
set_foreign_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	/* Call the FDW's GetForeignPaths function to generate path(s) */
	rel->fdwroutine->GetForeignPaths(root, rel, rte->relid);
}

/*
 * set_append_rel_size
 *	  Set size estimates for a simple "append relation"
 *
 * The passed-in rel and RTE represent the entire append relation.  The
 * relation's contents are computed by appending together the output of the
 * individual member relations.  Note that in the non-partitioned inheritance
 * case, the first member relation is actually the same table as is mentioned
 * in the parent RTE ... but it has a different RTE and RelOptInfo.  This is
 * a good thing because their outputs are not the same size.
 */
static void
set_append_rel_size(PlannerInfo *root, RelOptInfo *rel,
					Index rti, RangeTblEntry *rte)
{
	int			parentRTindex = rti;
	bool		has_live_children;
	double		parent_rows;
	double		parent_size;
	double	   *parent_attrsizes;
	int			nattrs;
	ListCell   *l;
#ifdef ADB
	int		   *subplan_ref;
	if (root->glob->subplans == NIL ||
		root->glob->clusterPlanOK == false)
		subplan_ref = NULL;
	else
		subplan_ref = palloc0(list_length(root->glob->subplans) * sizeof(subplan_ref[0]));
#endif /* ADB */

	Assert(IS_SIMPLE_REL(rel));

	/*
	 * Initialize to compute size estimates for whole append relation.
	 *
	 * We handle width estimates by weighting the widths of different child
	 * rels proportionally to their number of rows.  This is sensible because
	 * the use of width estimates is mainly to compute the total relation
	 * "footprint" if we have to sort or hash it.  To do this, we sum the
	 * total equivalent size (in "double" arithmetic) and then divide by the
	 * total rowcount estimate.  This is done separately for the total rel
	 * width and each attribute.
	 *
	 * Note: if you consider changing this logic, beware that child rels could
	 * have zero rows and/or width, if they were excluded by constraints.
	 */
	has_live_children = false;
	parent_rows = 0;
	parent_size = 0;
	nattrs = rel->max_attr - rel->min_attr + 1;
	parent_attrsizes = (double *) palloc0(nattrs * sizeof(double));

	foreach(l, root->append_rel_list)
	{
		AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);
		int			childRTindex;
		RangeTblEntry *childRTE;
		RelOptInfo *childrel;
		List	   *childquals;
		Index		cq_min_security;
		bool		have_const_false_cq;
		ListCell   *parentvars;
		ListCell   *childvars;
		ListCell   *lc;

		/* append_rel_list contains all append rels; ignore others */
		if (appinfo->parent_relid != parentRTindex)
			continue;

		childRTindex = appinfo->child_relid;
		childRTE = root->simple_rte_array[childRTindex];

		/*
		 * The child rel's RelOptInfo was already created during
		 * add_base_rels_to_query.
		 */
		childrel = find_base_rel(root, childRTindex);
		Assert(childrel->reloptkind == RELOPT_OTHER_MEMBER_REL);

		/*
		 * We have to copy the parent's targetlist and quals to the child,
		 * with appropriate substitution of variables.  However, only the
		 * baserestrictinfo quals are needed before we can check for
		 * constraint exclusion; so do that first and then check to see if we
		 * can disregard this child.
		 *
		 * The child rel's targetlist might contain non-Var expressions, which
		 * means that substitution into the quals could produce opportunities
		 * for const-simplification, and perhaps even pseudoconstant quals.
		 * Therefore, transform each RestrictInfo separately to see if it
		 * reduces to a constant or pseudoconstant.  (We must process them
		 * separately to keep track of the security level of each qual.)
		 */
		childquals = NIL;
		cq_min_security = UINT_MAX;
		have_const_false_cq = false;
		foreach(lc, rel->baserestrictinfo)
		{
			RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
			Node	   *childqual;
			ListCell   *lc2;

			Assert(IsA(rinfo, RestrictInfo));
			childqual = adjust_appendrel_attrs(root,
											   (Node *) rinfo->clause,
											   appinfo);
			childqual = eval_const_expressions(root, childqual);
			/* check for flat-out constant */
			if (childqual && IsA(childqual, Const))
			{
				if (((Const *) childqual)->constisnull ||
					!DatumGetBool(((Const *) childqual)->constvalue))
				{
					/* Restriction reduces to constant FALSE or NULL */
					have_const_false_cq = true;
					break;
				}
				/* Restriction reduces to constant TRUE, so drop it */
				continue;
			}
			/* might have gotten an AND clause, if so flatten it */
			foreach(lc2, make_ands_implicit((Expr *) childqual))
			{
				Node	   *onecq = (Node *) lfirst(lc2);
				bool		pseudoconstant;

				/* check for pseudoconstant (no Vars or volatile functions) */
				pseudoconstant =
					!contain_vars_of_level(onecq, 0) &&
					!contain_volatile_functions(onecq);
				if (pseudoconstant)
				{
					/* tell createplan.c to check for gating quals */
					root->hasPseudoConstantQuals = true;
				}
				/* reconstitute RestrictInfo with appropriate properties */
				childquals = lappend(childquals,
									 make_restrictinfo((Expr *) onecq,
													   rinfo->is_pushed_down,
													   rinfo->outerjoin_delayed,
													   pseudoconstant,
													   rinfo->security_level,
													   NULL, NULL, NULL));
				/* track minimum security level among child quals */
				cq_min_security = Min(cq_min_security, rinfo->security_level);
			}
		}

		/*
		 * In addition to the quals inherited from the parent, we might have
		 * securityQuals associated with this particular child node.
		 * (Currently this can only happen in appendrels originating from
		 * UNION ALL; inheritance child tables don't have their own
		 * securityQuals, see expand_inherited_rtentry().)	Pull any such
		 * securityQuals up into the baserestrictinfo for the child.  This is
		 * similar to process_security_barrier_quals() for the parent rel,
		 * except that we can't make any general deductions from such quals,
		 * since they don't hold for the whole appendrel.
		 */
		if (childRTE->securityQuals)
		{
			Index		security_level = 0;

			foreach(lc, childRTE->securityQuals)
			{
				List	   *qualset = (List *) lfirst(lc);
				ListCell   *lc2;

				foreach(lc2, qualset)
				{
					Expr	   *qual = (Expr *) lfirst(lc2);

					/* not likely that we'd see constants here, so no check */
					childquals = lappend(childquals,
										 make_restrictinfo(qual,
														   true, false, false,
														   security_level,
														   NULL, NULL, NULL));
					cq_min_security = Min(cq_min_security, security_level);
				}
				security_level++;
			}
			Assert(security_level <= root->qual_security_level);
		}

		/*
		 * OK, we've got all the baserestrictinfo quals for this child.
		 */
		childrel->baserestrictinfo = childquals;
		childrel->baserestrict_min_security = cq_min_security;

		if (have_const_false_cq)
		{
			/*
			 * Some restriction clause reduced to constant FALSE or NULL after
			 * substitution, so this child need not be scanned.
			 */
			set_dummy_rel_pathlist(childrel);
			continue;
		}

		if (relation_excluded_by_constraints(root, childrel, childRTE))
		{
			/*
			 * This child need not be scanned, so we can omit it from the
			 * appendrel.
			 */
			set_dummy_rel_pathlist(childrel);
			continue;
		}

		/*
		 * CE failed, so finish copying/modifying targetlist and join quals.
		 *
		 * NB: the resulting childrel->reltarget->exprs may contain arbitrary
		 * expressions, which otherwise would not occur in a rel's targetlist.
		 * Code that might be looking at an appendrel child must cope with
		 * such.  (Normally, a rel's targetlist would only include Vars and
		 * PlaceHolderVars.)  XXX we do not bother to update the cost or width
		 * fields of childrel->reltarget; not clear if that would be useful.
		 */
		childrel->joininfo = (List *)
			adjust_appendrel_attrs(root,
								   (Node *) rel->joininfo,
								   appinfo);
		childrel->reltarget->exprs = (List *)
			adjust_appendrel_attrs(root,
								   (Node *) rel->reltarget->exprs,
								   appinfo);

		/*
		 * We have to make child entries in the EquivalenceClass data
		 * structures as well.  This is needed either if the parent
		 * participates in some eclass joins (because we will want to consider
		 * inner-indexscan joins on the individual children) or if the parent
		 * has useful pathkeys (because we should try to build MergeAppend
		 * paths that produce those sort orderings).
		 */
		if (rel->has_eclass_joins || has_useful_pathkeys(root, rel))
			add_child_rel_equivalences(root, appinfo, rel, childrel);
		childrel->has_eclass_joins = rel->has_eclass_joins;

		/*
		 * Note: we could compute appropriate attr_needed data for the child's
		 * variables, by transforming the parent's attr_needed through the
		 * translated_vars mapping.  However, currently there's no need
		 * because attr_needed is only examined for base relations not
		 * otherrels.  So we just leave the child's attr_needed empty.
		 */

		/*
		 * If parallelism is allowable for this query in general, see whether
		 * it's allowable for this childrel in particular.  But if we've
		 * already decided the appendrel is not parallel-safe as a whole,
		 * there's no point in considering parallelism for this child.  For
		 * consistency, do this before calling set_rel_size() for the child.
		 */
		if (root->glob->parallelModeOK && rel->consider_parallel)
			set_rel_consider_parallel(root, childrel, childRTE);

		/*
		 * Compute the child's size.
		 */
		set_rel_size(root, childrel, childRTindex, childRTE);

		/*
		 * It is possible that constraint exclusion detected a contradiction
		 * within a child subquery, even though we didn't prove one above. If
		 * so, we can skip this child.
		 */
		if (IS_DUMMY_REL(childrel))
			continue;

#ifdef ADB
		if (subplan_ref)
			get_subplan_ref_walker((Expr*)childquals, subplan_ref);
#endif /* ADB */

		/* We have at least one live child. */
		has_live_children = true;

		/*
		 * If any live child is not parallel-safe, treat the whole appendrel
		 * as not parallel-safe.  In future we might be able to generate plans
		 * in which some children are farmed out to workers while others are
		 * not; but we don't have that today, so it's a waste to consider
		 * partial paths anywhere in the appendrel unless it's all safe.
		 * (Child rels visited before this one will be unmarked in
		 * set_append_rel_pathlist().)
		 */
		if (!childrel->consider_parallel)
			rel->consider_parallel = false;

		/*
		 * Accumulate size information from each live child.
		 */
		Assert(childrel->rows > 0);

		parent_rows += childrel->rows;
		parent_size += childrel->reltarget->width * childrel->rows;

		/*
		 * Accumulate per-column estimates too.  We need not do anything for
		 * PlaceHolderVars in the parent list.  If child expression isn't a
		 * Var, or we didn't record a width estimate for it, we have to fall
		 * back on a datatype-based estimate.
		 *
		 * By construction, child's targetlist is 1-to-1 with parent's.
		 */
		forboth(parentvars, rel->reltarget->exprs,
				childvars, childrel->reltarget->exprs)
		{
			Var		   *parentvar = (Var *) lfirst(parentvars);
			Node	   *childvar = (Node *) lfirst(childvars);

			if (IsA(parentvar, Var))
			{
				int			pndx = parentvar->varattno - rel->min_attr;
				int32		child_width = 0;

				if (IsA(childvar, Var) &&
					((Var *) childvar)->varno == childrel->relid)
				{
					int			cndx = ((Var *) childvar)->varattno - childrel->min_attr;

					child_width = childrel->attr_widths[cndx];
				}
				if (child_width <= 0)
					child_width = get_typavgwidth(exprType(childvar),
												  exprTypmod(childvar));
				Assert(child_width > 0);
				parent_attrsizes[pndx] += child_width * childrel->rows;
			}
		}
	}

	if (has_live_children)
	{
		/*
		 * Save the finished size estimates.
		 */
		int			i;

		Assert(parent_rows > 0);
		rel->rows = parent_rows;
		rel->reltarget->width = rint(parent_size / parent_rows);
		for (i = 0; i < nattrs; i++)
			rel->attr_widths[i] = rint(parent_attrsizes[i] / parent_rows);

		/*
		 * Set "raw tuples" count equal to "rows" for the appendrel; needed
		 * because some places assume rel->tuples is valid for any baserel.
		 */
		rel->tuples = parent_rows;

#ifdef ADB
		if (subplan_ref)
		{
			/*
			 * when ClusterReduce use more then one times, it not work(not support ReScan)
			 */
			ListCell *lc;
			int i = 0;
			foreach(lc, root->glob->subroots)
			{
				if (subplan_ref[i] > 1)
				{
					/* SubPlan's cluster plan is not created, just mark it need rewind */
					root->glob->rewindPlanIDs = bms_add_member(root->glob->rewindPlanIDs, i+1);
				}
				++i;
			}
		}
#endif /* ADB */
	}
	else
	{
		/*
		 * All children were excluded by constraints, so mark the whole
		 * appendrel dummy.  We must do this in this phase so that the rel's
		 * dummy-ness is visible when we generate paths for other rels.
		 */
		set_dummy_rel_pathlist(rel);
	}

	pfree(parent_attrsizes);
#ifdef ADB
	if (subplan_ref)
		pfree(subplan_ref);
#endif /* ADB */
}

/*
 * set_append_rel_pathlist
 *	  Build access paths for an "append relation"
 */
static void
set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
						Index rti, RangeTblEntry *rte)
{
	int			parentRTindex = rti;
	List	   *live_childrels = NIL;
	ListCell   *l;

	/*
	 * Generate access paths for each member relation, and remember the
	 * non-dummy children.
	 */
	foreach(l, root->append_rel_list)
	{
		AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);
		int			childRTindex;
		RangeTblEntry *childRTE;
		RelOptInfo *childrel;

		/* append_rel_list contains all append rels; ignore others */
		if (appinfo->parent_relid != parentRTindex)
			continue;

		/* Re-locate the child RTE and RelOptInfo */
		childRTindex = appinfo->child_relid;
		childRTE = root->simple_rte_array[childRTindex];
		childrel = root->simple_rel_array[childRTindex];

		/*
		 * If set_append_rel_size() decided the parent appendrel was
		 * parallel-unsafe at some point after visiting this child rel, we
		 * need to propagate the unsafety marking down to the child, so that
		 * we don't generate useless partial paths for it.
		 */
		if (!rel->consider_parallel)
			childrel->consider_parallel = false;

		/*
		 * Compute the child's access paths.
		 */
		set_rel_pathlist(root, childrel, childRTindex, childRTE);

		/*
		 * If child is dummy, ignore it.
		 */
		if (IS_DUMMY_REL(childrel))
			continue;

		/*
		 * Child is live, so add it to the live_childrels list for use below.
		 */
		live_childrels = lappend(live_childrels, childrel);
	}

	/* Add paths to the "append" relation. */
	add_paths_to_append_rel(root, rel, live_childrels);
}


/*
 * add_paths_to_append_rel
 *		Generate paths for given "append" relation given the set of non-dummy
 *		child rels.
 *
 * The function collects all parameterizations and orderings supported by the
 * non-dummy children. For every such parameterization or ordering, it creates
 * an append path collecting one path from each non-dummy child with given
 * parameterization or ordering. Similarly it collects partial paths from
 * non-dummy children to create partial append paths.
 */
static void
add_paths_to_append_rel(PlannerInfo *root, RelOptInfo *rel,
						List *live_childrels)
{
	List	   *subpaths = NIL;
	bool		subpaths_valid = true;
	List	   *partial_subpaths = NIL;
	bool		partial_subpaths_valid = true;
	List	   *all_child_pathkeys = NIL;
	List	   *all_child_outers = NIL;
	ListCell   *l;
	List	   *partitioned_rels = NIL;
	RangeTblEntry *rte;
	Path	   *path;
	RelOptInfo *childrel;
	bool		build_partitioned_rels = false;
#ifdef ADB
	List	   *reduce_list;
	List	   *reduce_var_map;
	List	   *all_reduce_by_val_list;
	List	   *all_replicate_oid;
	ReduceInfo *reduce_info;
	ListCell   *lc_pk;
	bool		have_reduce_coord = false;
	bool		have_final_replicate = false;
	bool		generate_partial;
#endif /* ADB */

	/*
	 * A plain relation will already have a PartitionedChildRelInfo if it is
	 * partitioned.  For a subquery RTE, no PartitionedChildRelInfo exists; we
	 * collect all partitioned_rels associated with any child.  (This assumes
	 * that we don't need to look through multiple levels of subquery RTEs; if
	 * we ever do, we could create a PartitionedChildRelInfo with the
	 * accumulated list of partitioned_rels which would then be found when
	 * populated our parent rel with paths.  For the present, that appears to
	 * be unnecessary.)
	 */
	rte = planner_rt_fetch(rel->relid, root);
	switch (rte->rtekind)
	{
		case RTE_RELATION:
			if (rte->relkind == RELKIND_PARTITIONED_TABLE)
			{
				partitioned_rels =
					get_partitioned_child_rels(root, rel->relid);
				Assert(list_length(partitioned_rels) >= 1);
			}
			break;
		case RTE_SUBQUERY:
			build_partitioned_rels = true;
			break;
		default:
			elog(ERROR, "unexpected rtekind: %d", (int) rte->rtekind);
	}

	/*
	 * For every non-dummy child, remember the cheapest path.  Also, identify
	 * all pathkeys (orderings) and parameterizations (required_outer sets)
	 * available for the non-dummy member relations.
	 */
	foreach(l, live_childrels)
	{
		ListCell   *lcp;
		childrel = lfirst(l);

		/*
		 * If we need to build partitioned_rels, accumulate the partitioned
		 * rels for this child.
		 */
		if (build_partitioned_rels)
		{
			List	   *cprels;

			cprels = get_partitioned_child_rels(root, childrel->relid);
			partitioned_rels = list_concat(partitioned_rels,
										   list_copy(cprels));
		}

		/*
		 * If child has an unparameterized cheapest-total path, add that to
		 * the unparameterized Append path we are constructing for the parent.
		 * If not, there's no workable unparameterized path.
		 */
		if (childrel->cheapest_total_path->param_info == NULL)
			subpaths = accumulate_append_subpath(subpaths,
												 childrel->cheapest_total_path);
		else
			subpaths_valid = false;

		/* Same idea, but for a partial plan. */
		if (childrel->partial_pathlist != NIL)
			partial_subpaths = accumulate_append_subpath(partial_subpaths,
														 linitial(childrel->partial_pathlist));
		else
			partial_subpaths_valid = false;

		/*
		 * Collect lists of all the available path orderings and
		 * parameterizations for all the children.  We use these as a
		 * heuristic to indicate which sort orderings and parameterizations we
		 * should build Append and MergeAppend paths for.
		 */
		foreach(lcp, childrel->pathlist)
		{
			Path	   *childpath = (Path *) lfirst(lcp);
			List	   *childkeys = childpath->pathkeys;
			Relids		childouter = PATH_REQ_OUTER(childpath);

			/* Unsorted paths don't contribute to pathkey list */
			if (childkeys != NIL)
			{
				ListCell   *lpk;
				bool		found = false;

				/* Have we already seen this ordering? */
				foreach(lpk, all_child_pathkeys)
				{
					List	   *existing_pathkeys = (List *) lfirst(lpk);

					if (compare_pathkeys(existing_pathkeys,
										 childkeys) == PATHKEYS_EQUAL)
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					/* No, so add it to all_child_pathkeys */
					all_child_pathkeys = lappend(all_child_pathkeys,
												 childkeys);
				}
			}

			/* Unparameterized paths don't contribute to param-set list */
			if (childouter)
			{
				ListCell   *lco;
				bool		found = false;

				/* Have we already seen this param set? */
				foreach(lco, all_child_outers)
				{
					Relids		existing_outers = (Relids) lfirst(lco);

					if (bms_equal(existing_outers, childouter))
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					/* No, so add it to all_child_outers */
					all_child_outers = lappend(all_child_outers,
											   childouter);
				}
			}
		}
	}

	/*
	 * If we found unparameterized paths for all children, build an unordered,
	 * unparameterized Append path for the rel.  (Note: this is correct even
	 * if we have zero or one live subpath due to constraint exclusion.)
	 */
	if (subpaths_valid)
		add_path(rel, (Path *) create_append_path(rel, subpaths, NULL, 0,
												  partitioned_rels));

	/*
	 * Consider an append of partial unordered, unparameterized partial paths.
	 */
	if (partial_subpaths_valid)
	{
		AppendPath *appendpath;
		ListCell   *lc;
		int			parallel_workers = 0;

		/*
		 * Decide on the number of workers to request for this append path.
		 * For now, we just use the maximum value from among the members.  It
		 * might be useful to use a higher number if the Append node were
		 * smart enough to spread out the workers, but it currently isn't.
		 */
		foreach(lc, partial_subpaths)
		{
			path = lfirst(lc);

			parallel_workers = Max(parallel_workers, path->parallel_workers);
		}
		Assert(parallel_workers > 0);

		/* Generate a partial append path. */
		appendpath = create_append_path(rel, partial_subpaths, NULL,
										parallel_workers, partitioned_rels);
		add_partial_path(rel, (Path *) appendpath);
	}

	/*
	 * Also build unparameterized MergeAppend paths based on the collected
	 * list of child pathkeys.
	 */
	if (subpaths_valid)
		generate_mergeappend_paths(root, rel, live_childrels,
								   all_child_pathkeys,
								   partitioned_rels);

	/*
	 * Build Append paths for each parameterization seen among the child rels.
	 * (This may look pretty expensive, but in most cases of practical
	 * interest, the child rels will expose mostly the same parameterizations,
	 * so that not that many cases actually get considered here.)
	 *
	 * The Append node itself cannot enforce quals, so all qual checking must
	 * be done in the child paths.  This means that to have a parameterized
	 * Append path, we must have the exact same parameterization for each
	 * child path; otherwise some children might be failing to check the
	 * moved-down quals.  To make them match up, we can try to increase the
	 * parameterization of lesser-parameterized paths.
	 */
	foreach(l, all_child_outers)
	{
		Relids		required_outer = (Relids) lfirst(l);
		ListCell   *lcr;

		/* Select the child paths for an Append with this parameterization */
		subpaths = NIL;
		subpaths_valid = true;
		foreach(lcr, live_childrels)
		{
			Path	   *subpath;
			childrel = (RelOptInfo *) lfirst(lcr);

			subpath = get_cheapest_parameterized_child_path(root,
															childrel,
															required_outer);
			if (subpath == NULL)
			{
				/* failed to make a suitable path for this child */
				subpaths_valid = false;
				break;
			}
			subpaths = accumulate_append_subpath(subpaths, subpath);
		}

		if (subpaths_valid)
			add_path(rel, (Path *)
					 create_append_path(rel, subpaths, required_outer, 0,
										partitioned_rels));
	}

#ifdef ADB
	generate_partial = false;
re_generate_append_:
	reduce_var_map = NIL;
	all_reduce_by_val_list = NIL;
	all_replicate_oid = NIL;
	all_child_pathkeys = NIL;
	all_child_outers = NIL;
	foreach(l, live_childrels)
	{
		ListCell *lc_path;
		childrel = lfirst(l);

		foreach(lc_path, generate_partial ? childrel->cluster_partial_pathlist : childrel->cluster_pathlist)
		{
			Relids		childouter;
			path = lfirst(lc_path);

			/* find all param paths */
			childouter = PATH_REQ_OUTER(path);
			if(childouter)
			{
				ListCell   *lco;
				bool		found = false;

				foreach(lco, all_child_outers)
				{
					if (bms_equal(lfirst(lco), childouter))
					{
						found = true;
						break;
					}
				}
				if (!found)
					all_child_outers = lappend(all_child_outers, childouter);
			}

			/* find all reduce info */
			reduce_list = get_reduce_info_list(path);
			if(IsReduceInfoListByValue(reduce_list))
			{
				ListCell *lc_path_reduce;
				ListCell *lc_saved_reduce;
				ReduceInfo *save_reduce;

				foreach(lc_path_reduce, reduce_list)
				{
					List *new_attno;
					reduce_info = lfirst(lc_path_reduce);
					Assert(IsReduceInfoByValue(reduce_info));

					/* find reduce column(s) target */
					new_attno = ReduceInfoFindPathTarget(reduce_info, path->pathtarget);
					if(new_attno == NIL)
					{
						/* lost target */
						continue;
					}

					foreach(lc_saved_reduce, all_reduce_by_val_list)
					{
						save_reduce = lfirst(lc_saved_reduce);
						if (IsReduceInfoSame(reduce_info, save_reduce))
							break;
					}
					if(lc_saved_reduce == NULL)
					{
						/* no saved */
						all_reduce_by_val_list = lappend(all_reduce_by_val_list, reduce_info);
						reduce_var_map = lappend(reduce_var_map, new_attno);
					}else
					{
						list_free(new_attno);
					}
				}
			}else if(IsReduceInfoListCoordinator(reduce_list))
			{
				have_reduce_coord = true;
			}else if(IsReduceInfoListFinalReplicated(reduce_list))
			{
				have_final_replicate = true;
			}else if(IsReduceInfoListReplicated(reduce_list))
			{
				/* remember all replicated nodes oid */
				Assert(list_length(reduce_list) == 1);
				reduce_info = linitial(reduce_list);
				all_replicate_oid = list_concat_unique_oid(all_replicate_oid, reduce_info->storage_nodes);
			}

			/* remember all pathkeys */
			if (path->pathkeys != NIL)
			{
				ListCell   *lpk;
				bool		found = false;

				foreach(lpk, all_child_pathkeys)
				{
					if (compare_pathkeys(lfirst(lpk), path->pathkeys) == PATHKEYS_EQUAL)
					{
						found = true;
						break;
					}
				}
				if (!found)
					all_child_pathkeys = lappend(all_child_pathkeys, path->pathkeys);
			}
		}
	}

	all_child_pathkeys = lcons(NIL, all_child_pathkeys);
	all_child_outers = lcons(NULL, all_child_outers);
	foreach(lc_pk, all_child_pathkeys)
	{
		ListCell	   *lc_new_attno;
		ReduceInfo	   *sub_reduce;
		ListCell	   *lc_rel;
		ListCell	   *lc_path;
		ListCell	   *lc_reduce;
		ListCell	   *lc_outer;
		Relids			req_outer;
		Path		   *test_path;
		Path		   *pathkey_path;
		List		   *pathkeys = lfirst(lc_pk);
		int				parallel_workers;
		bool			have_pathkeys_path;

		foreach(lc_outer, all_child_outers)
		{
			req_outer = lfirst(lc_outer);

			/* make redue by value AppendPath */
			forboth(l, all_reduce_by_val_list, lc_new_attno, reduce_var_map)
			{
				have_pathkeys_path = false;
				reduce_info = lfirst(l);
				subpaths = NIL;
				subpaths_valid = true;
				foreach(lc_rel, live_childrels)
				{
					childrel = lfirst(lc_rel);
					path = pathkey_path = NULL;
					/* find same reduce */
					foreach(lc_path, generate_partial ? childrel->cluster_partial_pathlist : childrel->cluster_pathlist)
					{
						test_path = lfirst(lc_path);
						if (bms_equal(PATH_REQ_OUTER(test_path), req_outer) == false)
							continue;
						reduce_list = get_reduce_info_list(test_path);
						if (!IsReduceInfoListByValue(reduce_list))
							continue;

						foreach(lc_reduce, reduce_list)
						{
							List *new_attno;
							sub_reduce = lfirst(lc_reduce);
							new_attno = ReduceInfoFindPathTarget(sub_reduce, test_path->pathtarget);
							if (equal(new_attno, lfirst(lc_new_attno)) &&
								IsReduceInfoSame(sub_reduce, reduce_info))
							{
								/* found match reduce */
								if (path == NULL ||
									compare_path_costs(path, test_path, TOTAL_COST) > 0)
								{
									path = test_path;
								}

								if (pathkeys &&
									pathkeys_contained_in(pathkeys, test_path->pathkeys) &&
									(pathkey_path == NULL ||
									 compare_path_costs(pathkey_path, test_path, TOTAL_COST) > 0))
								{
									pathkey_path = test_path;
								}
							}
							list_free(new_attno);
						}
					}

					if (pathkeys && pathkey_path)
					{
						path = pathkey_path;
						have_pathkeys_path = true;
					}

					if(path)
					{
						subpaths = accumulate_append_subpath(subpaths, path);
					}else
					{
						subpaths_valid = false;
						break;
					}
				}

				if (pathkeys && have_pathkeys_path == false)
					subpaths_valid = false;

				if(subpaths_valid)
				{
					/* make new reduce for AppendPath */
					List *params = MakeVarList(lfirst(lc_new_attno), rel->relid, rel->reltarget);
					parallel_workers = 0;
					Assert(params != NIL);
					sub_reduce = MakeReduceInfoAs(reduce_info, params);
					if (generate_partial)
						parallel_workers = get_max_parallel_workers(subpaths);

					if (pathkeys)
					{
						Assert(have_pathkeys_path);
						path = (Path*)create_merge_append_path(root,
															   rel,
															   subpaths,
															   pathkeys,
															   req_outer,
															   partitioned_rels);
						path->parallel_workers = parallel_workers;
					}else
					{
						path = (Path*)create_append_path(rel,
														 subpaths,
														 req_outer,
														 parallel_workers,
														 partitioned_rels);
					}
					path->reduce_info_list = list_make1(sub_reduce);
					path->reduce_is_valid = true;
					if (generate_partial)
						add_cluster_partial_path(rel, path);
					else
						add_cluster_path(rel, path);
				}else
				{
					list_free(subpaths);
				}
			} /* foreach all by value reduce info */

			if (pathkeys == NIL && req_outer == NULL && /* first loop */
				(generate_partial ? rel->cluster_partial_pathlist : rel->cluster_pathlist) == NIL)
			{
				/* make a none reduce path */
				List *storage = NIL;
				List *exclude = NIL;
				subpaths = NIL;
				subpaths_valid = true;
				foreach(l, live_childrels)
				{
					/* find cheapest path */
					childrel = lfirst(l);
					path = NULL;
					foreach(lc_path, generate_partial ? childrel->cluster_partial_pathlist : childrel->cluster_pathlist)
					{
						test_path = lfirst(lc_path);
						if(PATH_REQ_OUTER(test_path))
							continue;
						reduce_list = get_reduce_info_list(test_path);
						if (IsReduceInfoListCoordinator(reduce_list) ||
							IsReduceInfoListReplicated(reduce_list))
							continue;

						if (path == NULL ||
							compare_path_costs(path, test_path, TOTAL_COST) > 0)
						{
							path = test_path;
							foreach(lc_reduce, reduce_list)
							{
								reduce_info = lfirst(lc_reduce);
								storage = list_concat_unique_oid(storage, reduce_info->storage_nodes);
								exclude = list_concat_unique_oid(exclude, reduce_info->exclude_exec);
							}
						}
					}
					if(path == NULL)
					{
						subpaths_valid = false;
						break;
					}else
					{
						subpaths = accumulate_append_subpath(subpaths, path);
					}
				}
				if(subpaths_valid)
				{
					List *new_exclude = NIL;
					parallel_workers = 0;
					foreach(l, exclude)
					{
						if(!list_member_oid(storage, lfirst_oid(l)))
							new_exclude = lappend_oid(new_exclude, lfirst_oid(l));
					}
					reduce_info = MakeRandomReduceInfo(storage);
					reduce_info->exclude_exec = new_exclude;
					if (generate_partial)
						parallel_workers = get_max_parallel_workers(subpaths);
					path = (Path*)create_append_path(rel, subpaths, NULL, parallel_workers, partitioned_rels);
					path->reduce_info_list = list_make1(reduce_info);
					path->reduce_is_valid = true;
					if (generate_partial)
						add_cluster_partial_path(rel, path);
					else
						add_cluster_path(rel, path);
					list_free(storage);
					list_free(exclude);
				}
			}

#define MY_MAKE_PATH(reduce_list_test_fun_, new_reduce_info_)							\
	do																					\
	{																					\
		have_pathkeys_path = false;														\
		subpaths_valid = true;															\
		subpaths = NIL;																	\
		foreach(l, live_childrels)														\
		{																				\
			childrel = lfirst(l);														\
			path = pathkey_path = NULL;													\
			foreach(lc_path, generate_partial ? childrel->cluster_partial_pathlist : childrel->cluster_pathlist)	\
			{																			\
				test_path = lfirst(lc_path);											\
				if (bms_equal(PATH_REQ_OUTER(test_path), req_outer) == false ||			\
					!reduce_list_test_fun_(get_reduce_info_list(test_path)))			\
					continue;															\
																						\
				if (path == NULL ||														\
					compare_path_costs(path, test_path, TOTAL_COST) > 0)				\
				{																		\
					path = test_path;													\
				}																		\
																						\
				if (pathkeys &&															\
					pathkeys_contained_in(pathkeys, test_path->pathkeys) &&				\
					(pathkey_path == NULL ||											\
					 compare_path_costs(pathkey_path, test_path, TOTAL_COST) > 0))		\
				{																		\
					pathkey_path = test_path;											\
				}																		\
			}																			\
																						\
			if (pathkeys && pathkey_path)												\
			{																			\
				path = pathkey_path;													\
				have_pathkeys_path = true;												\
			}																			\
			if (path)																	\
			{																			\
				subpaths = accumulate_append_subpath(subpaths, path);					\
			}else																		\
			{																			\
				subpaths_valid = false;													\
				break;																	\
			}																			\
		}																				\
																						\
		if (pathkeys && have_pathkeys_path == false)									\
			subpaths_valid = false;														\
		if (subpaths_valid)																\
		{																				\
			parallel_workers = 0;														\
			if (generate_partial)														\
				parallel_workers = get_max_parallel_workers(subpaths);					\
			if (pathkeys)																\
			{																			\
				Assert(have_pathkeys_path);												\
				path = (Path*)create_merge_append_path(root,							\
													   rel,								\
													   subpaths,						\
													   pathkeys,						\
													   req_outer,						\
													   partitioned_rels);				\
				path->parallel_workers = parallel_workers;								\
			}else																		\
			{																			\
				path = (Path*)create_append_path(rel,									\
												 subpaths,								\
												 req_outer,								\
												 parallel_workers,						\
												 partitioned_rels);						\
			}																			\
			path->reduce_info_list = list_make1(new_reduce_info_);						\
			path->reduce_is_valid = true;												\
			if (generate_partial)														\
				add_cluster_partial_path(rel, path);									\
			else																		\
				add_cluster_path(rel, path);											\
		}																				\
	}while(0)

			if (have_reduce_coord)
			{
				/* make reduce to coordinator append path */
				MY_MAKE_PATH(IsReduceInfoListCoordinator,
							 MakeCoordinatorReduceInfo());
			}

			if (have_final_replicate)
			{
				MY_MAKE_PATH(IsReduceInfoListFinalReplicated,
							 MakeFinalReplicateReduceInfo());
			}

			if (all_replicate_oid)
			{
				#define MY_TEST_FUN(list_) IsReduceInfoListReplicatedOids((list_), all_replicate_oid, NIL)
				MY_MAKE_PATH(MY_TEST_FUN,
							 MakeReplicateReduceInfo(all_replicate_oid));
				#undef MY_TEST_FUN
			}
#undef MY_MAKE_PATH
		} /* foreach all_child_outers */
	} /* foreach all_child_pathkeys */

	if (rel->consider_parallel &&
		generate_partial == false)
	{
		generate_partial = true;
		goto re_generate_append_;
	}
#endif /* ADB */
}

/*
 * generate_mergeappend_paths
 *		Generate MergeAppend paths for an append relation
 *
 * Generate a path for each ordering (pathkey list) appearing in
 * all_child_pathkeys.
 *
 * We consider both cheapest-startup and cheapest-total cases, ie, for each
 * interesting ordering, collect all the cheapest startup subpaths and all the
 * cheapest total paths, and build a MergeAppend path for each case.
 *
 * We don't currently generate any parameterized MergeAppend paths.  While
 * it would not take much more code here to do so, it's very unclear that it
 * is worth the planning cycles to investigate such paths: there's little
 * use for an ordered path on the inside of a nestloop.  In fact, it's likely
 * that the current coding of add_path would reject such paths out of hand,
 * because add_path gives no credit for sort ordering of parameterized paths,
 * and a parameterized MergeAppend is going to be more expensive than the
 * corresponding parameterized Append path.  If we ever try harder to support
 * parameterized mergejoin plans, it might be worth adding support for
 * parameterized MergeAppends to feed such joins.  (See notes in
 * optimizer/README for why that might not ever happen, though.)
 */
static void
generate_mergeappend_paths(PlannerInfo *root, RelOptInfo *rel,
						   List *live_childrels,
						   List *all_child_pathkeys,
						   List *partitioned_rels)
{
	ListCell   *lcp;

	foreach(lcp, all_child_pathkeys)
	{
		List	   *pathkeys = (List *) lfirst(lcp);
		List	   *startup_subpaths = NIL;
		List	   *total_subpaths = NIL;
		bool		startup_neq_total = false;
		ListCell   *lcr;

		/* Select the child paths for this ordering... */
		foreach(lcr, live_childrels)
		{
			RelOptInfo *childrel = (RelOptInfo *) lfirst(lcr);
			Path	   *cheapest_startup,
					   *cheapest_total;

			/* Locate the right paths, if they are available. */
			cheapest_startup =
				get_cheapest_path_for_pathkeys(childrel->pathlist,
											   pathkeys,
											   NULL,
											   STARTUP_COST,
											   false);
			cheapest_total =
				get_cheapest_path_for_pathkeys(childrel->pathlist,
											   pathkeys,
											   NULL,
											   TOTAL_COST,
											   false);

			/*
			 * If we can't find any paths with the right order just use the
			 * cheapest-total path; we'll have to sort it later.
			 */
			if (cheapest_startup == NULL || cheapest_total == NULL)
			{
				cheapest_startup = cheapest_total =
					childrel->cheapest_total_path;
				/* Assert we do have an unparameterized path for this child */
				Assert(cheapest_total->param_info == NULL);
			}

			/*
			 * Notice whether we actually have different paths for the
			 * "cheapest" and "total" cases; frequently there will be no point
			 * in two create_merge_append_path() calls.
			 */
			if (cheapest_startup != cheapest_total)
				startup_neq_total = true;

			startup_subpaths =
				accumulate_append_subpath(startup_subpaths, cheapest_startup);
			total_subpaths =
				accumulate_append_subpath(total_subpaths, cheapest_total);
		}

		/* ... and build the MergeAppend paths */
		add_path(rel, (Path *) create_merge_append_path(root,
														rel,
														startup_subpaths,
														pathkeys,
														NULL,
														partitioned_rels));
		if (startup_neq_total)
			add_path(rel, (Path *) create_merge_append_path(root,
															rel,
															total_subpaths,
															pathkeys,
															NULL,
															partitioned_rels));
	}
}

/*
 * get_cheapest_parameterized_child_path
 *		Get cheapest path for this relation that has exactly the requested
 *		parameterization.
 *
 * Returns NULL if unable to create such a path.
 */
static Path *
get_cheapest_parameterized_child_path(PlannerInfo *root, RelOptInfo *rel,
									  Relids required_outer)
{
	Path	   *cheapest;
	ListCell   *lc;

	/*
	 * Look up the cheapest existing path with no more than the needed
	 * parameterization.  If it has exactly the needed parameterization, we're
	 * done.
	 */
	cheapest = get_cheapest_path_for_pathkeys(rel->pathlist,
											  NIL,
											  required_outer,
											  TOTAL_COST,
											  false);
	Assert(cheapest != NULL);
	if (bms_equal(PATH_REQ_OUTER(cheapest), required_outer))
		return cheapest;

	/*
	 * Otherwise, we can "reparameterize" an existing path to match the given
	 * parameterization, which effectively means pushing down additional
	 * joinquals to be checked within the path's scan.  However, some existing
	 * paths might check the available joinquals already while others don't;
	 * therefore, it's not clear which existing path will be cheapest after
	 * reparameterization.  We have to go through them all and find out.
	 */
	cheapest = NULL;
	foreach(lc, rel->pathlist)
	{
		Path	   *path = (Path *) lfirst(lc);

		/* Can't use it if it needs more than requested parameterization */
		if (!bms_is_subset(PATH_REQ_OUTER(path), required_outer))
			continue;

		/*
		 * Reparameterization can only increase the path's cost, so if it's
		 * already more expensive than the current cheapest, forget it.
		 */
		if (cheapest != NULL &&
			compare_path_costs(cheapest, path, TOTAL_COST) <= 0)
			continue;

		/* Reparameterize if needed, then recheck cost */
		if (!bms_equal(PATH_REQ_OUTER(path), required_outer))
		{
			path = reparameterize_path(root, path, required_outer, 1.0);
			if (path == NULL)
				continue;		/* failed to reparameterize this one */
			Assert(bms_equal(PATH_REQ_OUTER(path), required_outer));

			if (cheapest != NULL &&
				compare_path_costs(cheapest, path, TOTAL_COST) <= 0)
				continue;
		}

		/* We have a new best path */
		cheapest = path;
	}

	/* Return the best path, or NULL if we found no suitable candidate */
	return cheapest;
}

/*
 * accumulate_append_subpath
 *		Add a subpath to the list being built for an Append or MergeAppend
 *
 * It's possible that the child is itself an Append or MergeAppend path, in
 * which case we can "cut out the middleman" and just add its child paths to
 * our own list.  (We don't try to do this earlier because we need to apply
 * both levels of transformation to the quals.)
 *
 * Note that if we omit a child MergeAppend in this way, we are effectively
 * omitting a sort step, which seems fine: if the parent is to be an Append,
 * its result would be unsorted anyway, while if the parent is to be a
 * MergeAppend, there's no point in a separate sort on a child.
 */
static List *
accumulate_append_subpath(List *subpaths, Path *path)
{
	if (IsA(path, AppendPath))
	{
		AppendPath *apath = (AppendPath *) path;

		/* list_copy is important here to avoid sharing list substructure */
		return list_concat(subpaths, list_copy(apath->subpaths));
	}
	else if (IsA(path, MergeAppendPath))
	{
		MergeAppendPath *mpath = (MergeAppendPath *) path;

		/* list_copy is important here to avoid sharing list substructure */
		return list_concat(subpaths, list_copy(mpath->subpaths));
	}
	else
		return lappend(subpaths, path);
}

/*
 * set_dummy_rel_pathlist
 *	  Build a dummy path for a relation that's been excluded by constraints
 *
 * Rather than inventing a special "dummy" path type, we represent this as an
 * AppendPath with no members (see also IS_DUMMY_PATH/IS_DUMMY_REL macros).
 *
 * This is exported because inheritance_planner() has need for it.
 */
void
set_dummy_rel_pathlist(RelOptInfo *rel)
{
	/* Set dummy size estimates --- we leave attr_widths[] as zeroes */
	rel->rows = 0;
	rel->reltarget->width = 0;

	/* Discard any pre-existing paths; no further need for them */
	rel->pathlist = NIL;
	rel->partial_pathlist = NIL;

	add_path(rel, (Path *) create_append_path(rel, NIL, NULL, 0, NIL));

	/*
	 * We set the cheapest path immediately, to ensure that IS_DUMMY_REL()
	 * will recognize the relation as dummy if anyone asks.  This is redundant
	 * when we're called from set_rel_size(), but not when called from
	 * elsewhere, and doing it twice is harmless anyway.
	 */
	set_cheapest(rel);
}

/* quick-and-dirty test to see if any joining is needed */
static bool
has_multiple_baserels(PlannerInfo *root)
{
	int			num_base_rels = 0;
	Index		rti;

	for (rti = 1; rti < root->simple_rel_array_size; rti++)
	{
		RelOptInfo *brel = root->simple_rel_array[rti];

		if (brel == NULL)
			continue;

		/* ignore RTEs that are "other rels" */
		if (brel->reloptkind == RELOPT_BASEREL)
			if (++num_base_rels > 1)
				return true;
	}
	return false;
}

/*
 * set_subquery_pathlist
 *		Generate SubqueryScan access paths for a subquery RTE
 *
 * We don't currently support generating parameterized paths for subqueries
 * by pushing join clauses down into them; it seems too expensive to re-plan
 * the subquery multiple times to consider different alternatives.
 * (XXX that could stand to be reconsidered, now that we use Paths.)
 * So the paths made here will be parameterized if the subquery contains
 * LATERAL references, otherwise not.  As long as that's true, there's no need
 * for a separate set_subquery_size phase: just make the paths right away.
 */
static void
set_subquery_pathlist(PlannerInfo *root, RelOptInfo *rel,
					  Index rti, RangeTblEntry *rte)
{
	Query	   *parse = root->parse;
	Query	   *subquery = rte->subquery;
	Relids		required_outer;
	pushdown_safety_info safetyInfo;
	double		tuple_fraction;
	RelOptInfo *sub_final_rel;
	ListCell   *lc;
#ifdef ADB
	bool need_limit;
#endif

	/*
	 * Must copy the Query so that planning doesn't mess up the RTE contents
	 * (really really need to fix the planner to not scribble on its input,
	 * someday ... but see remove_unused_subquery_outputs to start with).
	 */
	subquery = copyObject(subquery);

	/*
	 * If it's a LATERAL subquery, it might contain some Vars of the current
	 * query level, requiring it to be treated as parameterized, even though
	 * we don't support pushing down join quals into subqueries.
	 */
	required_outer = rel->lateral_relids;

	/*
	 * Zero out result area for subquery_is_pushdown_safe, so that it can set
	 * flags as needed while recursing.  In particular, we need a workspace
	 * for keeping track of unsafe-to-reference columns.  unsafeColumns[i]
	 * will be set TRUE if we find that output column i of the subquery is
	 * unsafe to use in a pushed-down qual.
	 */
	memset(&safetyInfo, 0, sizeof(safetyInfo));
	safetyInfo.unsafeColumns = (bool *)
		palloc0((list_length(subquery->targetList) + 1) * sizeof(bool));

	/*
	 * If the subquery has the "security_barrier" flag, it means the subquery
	 * originated from a view that must enforce row level security.  Then we
	 * must not push down quals that contain leaky functions.  (Ideally this
	 * would be checked inside subquery_is_pushdown_safe, but since we don't
	 * currently pass the RTE to that function, we must do it here.)
	 */
	safetyInfo.unsafeLeaky = rte->security_barrier;

	/*
	 * If there are any restriction clauses that have been attached to the
	 * subquery relation, consider pushing them down to become WHERE or HAVING
	 * quals of the subquery itself.  This transformation is useful because it
	 * may allow us to generate a better plan for the subquery than evaluating
	 * all the subquery output rows and then filtering them.
	 *
	 * There are several cases where we cannot push down clauses. Restrictions
	 * involving the subquery are checked by subquery_is_pushdown_safe().
	 * Restrictions on individual clauses are checked by
	 * qual_is_pushdown_safe().  Also, we don't want to push down
	 * pseudoconstant clauses; better to have the gating node above the
	 * subquery.
	 *
	 * Non-pushed-down clauses will get evaluated as qpquals of the
	 * SubqueryScan node.
	 *
	 * XXX Are there any cases where we want to make a policy decision not to
	 * push down a pushable qual, because it'd result in a worse plan?
	 */
	if (rel->baserestrictinfo != NIL &&
		subquery_is_pushdown_safe(subquery, subquery, &safetyInfo))
	{
		/* OK to consider pushing down individual quals */
		List	   *upperrestrictlist = NIL;
		ListCell   *l;

		foreach(l, rel->baserestrictinfo)
		{
			RestrictInfo *rinfo = (RestrictInfo *) lfirst(l);
			Node	   *clause = (Node *) rinfo->clause;

			if (!rinfo->pseudoconstant &&
				qual_is_pushdown_safe(subquery, rti, clause, &safetyInfo))
			{
				/* Push it down */
				subquery_push_qual(subquery, rte, rti, clause);
			}
			else
			{
				/* Keep it in the upper query */
				upperrestrictlist = lappend(upperrestrictlist, rinfo);
			}
		}
		rel->baserestrictinfo = upperrestrictlist;
		/* We don't bother recomputing baserestrict_min_security */
	}

	pfree(safetyInfo.unsafeColumns);

	/*
	 * The upper query might not use all the subquery's output columns; if
	 * not, we can simplify.
	 */
	remove_unused_subquery_outputs(subquery, rel);

	/*
	 * We can safely pass the outer tuple_fraction down to the subquery if the
	 * outer level has no joining, aggregation, or sorting to do. Otherwise
	 * we'd better tell the subquery to plan for full retrieval. (XXX This
	 * could probably be made more intelligent ...)
	 */
	if (parse->hasAggs ||
		parse->groupClause ||
		parse->groupingSets ||
		parse->havingQual ||
		parse->distinctClause ||
		parse->sortClause ||
		has_multiple_baserels(root))
		tuple_fraction = 0.0;	/* default case */
	else
		tuple_fraction = root->tuple_fraction;

	/* plan_params should not be in use in current query level */
	Assert(root->plan_params == NIL);

	/* Generate a subroot and Paths for the subquery */
	rel->subroot = subquery_planner(root->glob, subquery,
									root,
									false, tuple_fraction);

	/* Isolate the params needed by this specific subplan */
	rel->subplan_params = root->plan_params;
	root->plan_params = NIL;

	/*
	 * It's possible that constraint exclusion proved the subquery empty. If
	 * so, it's desirable to produce an unadorned dummy path so that we will
	 * recognize appropriate optimizations at this query level.
	 */
	sub_final_rel = fetch_upper_rel(rel->subroot, UPPERREL_FINAL, NULL);

	if (IS_DUMMY_REL(sub_final_rel))
	{
		set_dummy_rel_pathlist(rel);
		return;
	}

	/*
	 * Mark rel with estimated output rows, width, etc.  Note that we have to
	 * do this before generating outer-query paths, else cost_subqueryscan is
	 * not happy.
	 */
	set_subquery_size_estimates(root, rel);

	/*
	 * For each Path that subquery_planner produced, make a SubqueryScanPath
	 * in the outer query.
	 */
	foreach(lc, sub_final_rel->pathlist)
	{
		Path	   *subpath = (Path *) lfirst(lc);
		List	   *pathkeys;

		/* Convert subpath's pathkeys to outer representation */
		pathkeys = convert_subquery_pathkeys(root,
											 rel,
											 subpath->pathkeys,
											 make_tlist_from_pathtarget(subpath->pathtarget));

		/* Generate outer path using this subpath */
		add_path(rel, (Path *)
				 create_subqueryscan_path(root, rel, subpath,
										  pathkeys, required_outer));
	}
#ifdef ADB
	need_limit = limit_needed(subquery);
	foreach(lc, sub_final_rel->cluster_pathlist)
	{
		Path	   *subpath = (Path *) lfirst(lc);
		List	   *pathkeys;

		/* Convert subpath's pathkeys to outer representation */
		pathkeys = convert_subquery_pathkeys(root,
											 rel,
											 subpath->pathkeys,
							make_tlist_from_pathtarget(subpath->pathtarget));

		if ((need_limit || subquery->sortClause) &&
			!IsReduceInfoListReplicated(get_reduce_info_list(subpath)) &&
			!IsReduceInfoListInOneNode(get_reduce_info_list(subpath)))
		{
			subpath = create_cluster_reduce_path(root,
												 subpath,
												 list_make1(MakeCoordinatorReduceInfo()),
												 sub_final_rel,
												 NIL);
			if(subquery->sortClause)
			{
				/* we have no mergereduce, sort it again */
				subpath = (Path*)
						  create_sort_path(root,
										   sub_final_rel,
										   subpath,
										   rel->subroot->sort_pathkeys,
										   -1.0);
			}
			if(need_limit)
			{
				/* we need limit again */
				subpath = (Path*)
						  create_limit_path(root,
											rel,
											subpath,
											subquery->limitOffset,
											subquery->limitCount,
											0,0);
			}
		}

		/* Generate outer path using this subpath */
		add_cluster_path(rel, (Path *)
				 create_subqueryscan_path(root, rel, subpath,
										  pathkeys, required_outer));
	}
#endif /* ADB */
}

/*
 * set_function_pathlist
 *		Build the (single) access path for a function RTE
 */
static void
set_function_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;
	List	   *pathkeys = NIL;

	/*
	 * We don't support pushing join clauses into the quals of a function
	 * scan, but it could still have required parameterization due to LATERAL
	 * refs in the function expression.
	 */
	required_outer = rel->lateral_relids;

	/*
	 * The result is considered unordered unless ORDINALITY was used, in which
	 * case it is ordered by the ordinal column (the last one).  See if we
	 * care, by checking for uses of that Var in equivalence classes.
	 */
	if (rte->funcordinality)
	{
		AttrNumber	ordattno = rel->max_attr;
		Var		   *var = NULL;
		ListCell   *lc;

		/*
		 * Is there a Var for it in rel's targetlist?  If not, the query did
		 * not reference the ordinality column, or at least not in any way
		 * that would be interesting for sorting.
		 */
		foreach(lc, rel->reltarget->exprs)
		{
			Var		   *node = (Var *) lfirst(lc);

			/* checking varno/varlevelsup is just paranoia */
			if (IsA(node, Var) &&
				node->varattno == ordattno &&
				node->varno == rel->relid &&
				node->varlevelsup == 0)
			{
				var = node;
				break;
			}
		}

		/*
		 * Try to build pathkeys for this Var with int8 sorting.  We tell
		 * build_expression_pathkey not to build any new equivalence class; if
		 * the Var isn't already mentioned in some EC, it means that nothing
		 * cares about the ordering.
		 */
		if (var)
			pathkeys = build_expression_pathkey(root,
												(Expr *) var,
												NULL,	/* below outer joins */
												Int8LessOperator,
												rel->relids,
												false);
	}

	/* Generate appropriate path */
	add_path(rel, create_functionscan_path(root, rel,
										   pathkeys, required_outer));
}

/*
 * set_values_pathlist
 *		Build the (single) access path for a VALUES RTE
 */
static void
set_values_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;

	/*
	 * We don't support pushing join clauses into the quals of a values scan,
	 * but it could still have required parameterization due to LATERAL refs
	 * in the values expressions.
	 */
	required_outer = rel->lateral_relids;

	/* Generate appropriate path */
	add_path(rel, create_valuesscan_path(root, rel, required_outer));
}

/*
 * set_tablefunc_pathlist
 *		Build the (single) access path for a table func RTE
 */
static void
set_tablefunc_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;

	/*
	 * We don't support pushing join clauses into the quals of a tablefunc
	 * scan, but it could still have required parameterization due to LATERAL
	 * refs in the function expression.
	 */
	required_outer = rel->lateral_relids;

	/* Generate appropriate path */
	add_path(rel, create_tablefuncscan_path(root, rel,
											required_outer));
}

/*
 * set_cte_pathlist
 *		Build the (single) access path for a non-self-reference CTE RTE
 *
 * There's no need for a separate set_cte_size phase, since we don't
 * support join-qual-parameterized paths for CTEs.
 */
static void
set_cte_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Plan	   *cteplan;
	PlannerInfo *cteroot;
	Index		levelsup;
	int			ndx;
	ListCell   *lc;
	int			plan_id;
	Relids		required_outer;

	/*
	 * Find the referenced CTE, and locate the plan previously made for it.
	 */
	levelsup = rte->ctelevelsup;
	cteroot = root;
	while (levelsup-- > 0)
	{
		cteroot = cteroot->parent_root;
		if (!cteroot)			/* shouldn't happen */
			elog(ERROR, "bad levelsup for CTE \"%s\"", rte->ctename);
	}

	/*
	 * Note: cte_plan_ids can be shorter than cteList, if we are still working
	 * on planning the CTEs (ie, this is a side-reference from another CTE).
	 * So we mustn't use forboth here.
	 */
	ndx = 0;
	foreach(lc, cteroot->parse->cteList)
	{
		CommonTableExpr *cte = (CommonTableExpr *) lfirst(lc);

		if (strcmp(cte->ctename, rte->ctename) == 0)
			break;
		ndx++;
	}
	if (lc == NULL)				/* shouldn't happen */
		elog(ERROR, "could not find CTE \"%s\"", rte->ctename);
	if (ndx >= list_length(cteroot->cte_plan_ids))
		elog(ERROR, "could not find plan for CTE \"%s\"", rte->ctename);
	plan_id = list_nth_int(cteroot->cte_plan_ids, ndx);
	Assert(plan_id > 0);
	cteplan = (Plan *) list_nth(root->glob->subplans, plan_id - 1);

	/* Mark rel with estimated output rows, width, etc */
	set_cte_size_estimates(root, rel, cteplan->plan_rows);

	/*
	 * We don't support pushing join clauses into the quals of a CTE scan, but
	 * it could still have required parameterization due to LATERAL refs in
	 * its tlist.
	 */
	required_outer = rel->lateral_relids;

	/* Generate appropriate path */
	add_path(rel, create_ctescan_path(root, rel, required_outer));
#ifdef ADB
	if(root->glob->clusterPlanOK && bms_is_empty(required_outer))
	{
		PlannerInfo *subroot = list_nth(root->glob->subroots, plan_id - 1);
		RelOptInfo *final_rel = fetch_upper_rel(subroot, UPPERREL_FINAL, NULL);
		if(final_rel->cheapest_cluster_total_path)
		{
			Path *cheapest_path = final_rel->cheapest_cluster_total_path;
			Path *cluster_path = create_ctescan_path(root, rel, NULL);
			Path *normal_path = linitial(rel->pathlist);
			List *reduce_info_list = get_reduce_info_list(cheapest_path);

			/* we need have diffent cost for cluster path and not cluster path */
			normal_path->startup_cost = cteplan->startup_cost;
			normal_path->total_cost = cteplan->total_cost;
			cluster_path->startup_cost = cheapest_path->startup_cost;
			cluster_path->total_cost = cheapest_path->total_cost;
			cluster_path->rows = cheapest_path->rows;

			cluster_path->reduce_info_list = ConvertReduceInfoList(reduce_info_list,
														   subroot->upper_targets[UPPERREL_FINAL],
														   rel->relid);
			cluster_path->reduce_is_valid = true;
			add_cluster_path(rel, cluster_path);
		}
	}
#endif /* ADB */
}

#ifdef ADB
static void set_paramtuplestore_pathlist(PlannerInfo *root, RelOptInfo *rel,
							 RangeTblEntry *rte)
{
	Relids required_outer;
	Path *path;
	rel->tuples = rte->rows;
	set_baserel_size_estimates(root, rel);

	/*
	 * We don't support pushing join clauses into the quals of a tuplestore
	 * scan, but it could still have required parameterization due to LATERAL
	 * refs in its tlist.
	 */
	required_outer = rel->lateral_relids;

	/* Generate path */
	path = create_paramtuplestorescan_path(root, rel, required_outer);
	path->reduce_info_list = list_make1(MakeCoordinatorReduceInfo());
	path->reduce_is_valid = true;
	add_path(rel, path);

	/* generate cluster path */
	if (root->glob->clusterPlanOK)
	{
		path = create_paramtuplestorescan_path(root, rel, required_outer);
		path->reduce_info_list = list_make1(MakeRandomReduceInfo(rte->execNodes));
		path->reduce_is_valid = true;
		add_cluster_path(rel, path);
	}

	/* Select cheapest path (pretty easy in this case...) */
	set_cheapest(rel);

}

#endif /* ADB */

/*
 * set_namedtuplestore_pathlist
 *		Build the (single) access path for a named tuplestore RTE
 *
 * There's no need for a separate set_namedtuplestore_size phase, since we
 * don't support join-qual-parameterized paths for tuplestores.
 */
static void
set_namedtuplestore_pathlist(PlannerInfo *root, RelOptInfo *rel,
							 RangeTblEntry *rte)
{
	Relids		required_outer;

	/* Mark rel with estimated output rows, width, etc */
	set_namedtuplestore_size_estimates(root, rel);

	/*
	 * We don't support pushing join clauses into the quals of a tuplestore
	 * scan, but it could still have required parameterization due to LATERAL
	 * refs in its tlist.
	 */
	required_outer = rel->lateral_relids;

	/* Generate appropriate path */
	add_path(rel, create_namedtuplestorescan_path(root, rel, required_outer));

	/* Select cheapest path (pretty easy in this case...) */
	set_cheapest(rel);
}

/*
 * set_worktable_pathlist
 *		Build the (single) access path for a self-reference CTE RTE
 *
 * There's no need for a separate set_worktable_size phase, since we don't
 * support join-qual-parameterized paths for CTEs.
 */
static void
set_worktable_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Path	   *ctepath;
	PlannerInfo *cteroot;
	Index		levelsup;
	Relids		required_outer;

	/*
	 * We need to find the non-recursive term's path, which is in the plan
	 * level that's processing the recursive UNION, which is one level *below*
	 * where the CTE comes from.
	 */
	levelsup = rte->ctelevelsup;
	if (levelsup == 0)			/* shouldn't happen */
		elog(ERROR, "bad levelsup for CTE \"%s\"", rte->ctename);
	levelsup--;
	cteroot = root;
	while (levelsup-- > 0)
	{
		cteroot = cteroot->parent_root;
		if (!cteroot)			/* shouldn't happen */
			elog(ERROR, "bad levelsup for CTE \"%s\"", rte->ctename);
	}
	ctepath = cteroot->non_recursive_path;
	if (!ctepath)				/* shouldn't happen */
		elog(ERROR, "could not find path for CTE \"%s\"", rte->ctename);

	/* Mark rel with estimated output rows, width, etc */
	set_cte_size_estimates(root, rel, ctepath->rows);

	/*
	 * We don't support pushing join clauses into the quals of a worktable
	 * scan, but it could still have required parameterization due to LATERAL
	 * refs in its tlist.  (I'm not sure this is actually possible given the
	 * restrictions on recursive references, but it's easy enough to support.)
	 */
	required_outer = rel->lateral_relids;

	/* Generate appropriate path */
	add_path(rel, create_worktablescan_path(root, rel, required_outer));
}

/*
 * generate_gather_paths
 *		Generate parallel access paths for a relation by pushing a Gather or
 *		Gather Merge on top of a partial path.
 *
 * This must not be called until after we're done creating all partial paths
 * for the specified relation.  (Otherwise, add_partial_path might delete a
 * path that some GatherPath or GatherMergePath has a reference to.)
 */
void
generate_gather_paths(PlannerInfo *root, RelOptInfo *rel)
{
	Path	   *cheapest_partial_path;
	Path	   *simple_gather_path;
	ListCell   *lc;

	/* If there are no partial paths, there's nothing to do here. */
	if (rel->partial_pathlist == NIL)
#ifdef ADB
		goto generate_cluster_gather_;
#else
		return;
#endif /* ADB */

	/*
	 * The output of Gather is always unsorted, so there's only one partial
	 * path of interest: the cheapest one.  That will be the one at the front
	 * of partial_pathlist because of the way add_partial_path works.
	 */
	cheapest_partial_path = linitial(rel->partial_pathlist);
	simple_gather_path = (Path *)
		create_gather_path(root, rel, cheapest_partial_path, rel->reltarget,
						   NULL, NULL);
	add_path(rel, simple_gather_path);

	/*
	 * For each useful ordering, we can consider an order-preserving Gather
	 * Merge.
	 */
	foreach(lc, rel->partial_pathlist)
	{
		Path	   *subpath = (Path *) lfirst(lc);
		GatherMergePath *path;

		if (subpath->pathkeys == NIL)
			continue;

		path = create_gather_merge_path(root, rel, subpath, rel->reltarget,
										subpath->pathkeys, NULL, NULL);
		add_path(rel, &path->path);
	}
#ifdef ADB
generate_cluster_gather_:
	foreach(lc, rel->cluster_partial_pathlist)
	{
		Path   *subpath = lfirst(lc);
		double rows = subpath->rows * subpath->parallel_workers;
		Path   *path = (Path*)create_gather_path(root,
												 rel,
												 subpath,
												 rel->reltarget,
												 NULL, &rows);
		path->reduce_info_list = CopyReduceInfoList(get_reduce_info_list(subpath));
		path->reduce_is_valid = true;
		add_cluster_path(rel, path);

		if(subpath->pathkeys)
		{
			path = (Path*)create_gather_merge_path(root, rel, subpath, rel->reltarget,
												   subpath->pathkeys, NULL, &rows);
			path->reduce_info_list = CopyReduceInfoList(get_reduce_info_list(subpath));
			path->reduce_is_valid = true;
			add_cluster_path(rel, path);
		}
	}
#endif /* ADB */
}

/*
 * make_rel_from_joinlist
 *	  Build access paths using a "joinlist" to guide the join path search.
 *
 * See comments for deconstruct_jointree() for definition of the joinlist
 * data structure.
 */
static RelOptInfo *
make_rel_from_joinlist(PlannerInfo *root, List *joinlist)
{
	int			levels_needed;
	List	   *initial_rels;
	ListCell   *jl;

	/*
	 * Count the number of child joinlist nodes.  This is the depth of the
	 * dynamic-programming algorithm we must employ to consider all ways of
	 * joining the child nodes.
	 */
	levels_needed = list_length(joinlist);

	if (levels_needed <= 0)
		return NULL;			/* nothing to do? */

	/*
	 * Construct a list of rels corresponding to the child joinlist nodes.
	 * This may contain both base rels and rels constructed according to
	 * sub-joinlists.
	 */
	initial_rels = NIL;
	foreach(jl, joinlist)
	{
		Node	   *jlnode = (Node *) lfirst(jl);
		RelOptInfo *thisrel;

		if (IsA(jlnode, RangeTblRef))
		{
			int			varno = ((RangeTblRef *) jlnode)->rtindex;

			thisrel = find_base_rel(root, varno);
		}
		else if (IsA(jlnode, List))
		{
			/* Recurse to handle subproblem */
			thisrel = make_rel_from_joinlist(root, (List *) jlnode);
		}
		else
		{
			elog(ERROR, "unrecognized joinlist node type: %d",
				 (int) nodeTag(jlnode));
			thisrel = NULL;		/* keep compiler quiet */
		}

		initial_rels = lappend(initial_rels, thisrel);
	}

	if (levels_needed == 1)
	{
		/*
		 * Single joinlist node, so we're done.
		 */
		return (RelOptInfo *) linitial(initial_rels);
	}
	else
	{
		/*
		 * Consider the different orders in which we could join the rels,
		 * using a plugin, GEQO, or the regular join search code.
		 *
		 * We put the initial_rels list into a PlannerInfo field because
		 * has_legal_joinclause() needs to look at it (ugly :-().
		 */
		root->initial_rels = initial_rels;

		if (join_search_hook)
			return (*join_search_hook) (root, levels_needed, initial_rels);
		else if (enable_geqo && levels_needed >= geqo_threshold)
			return geqo(root, levels_needed, initial_rels);
		else
			return standard_join_search(root, levels_needed, initial_rels);
	}
}

/*
 * standard_join_search
 *	  Find possible joinpaths for a query by successively finding ways
 *	  to join component relations into join relations.
 *
 * 'levels_needed' is the number of iterations needed, ie, the number of
 *		independent jointree items in the query.  This is > 1.
 *
 * 'initial_rels' is a list of RelOptInfo nodes for each independent
 *		jointree item.  These are the components to be joined together.
 *		Note that levels_needed == list_length(initial_rels).
 *
 * Returns the final level of join relations, i.e., the relation that is
 * the result of joining all the original relations together.
 * At least one implementation path must be provided for this relation and
 * all required sub-relations.
 *
 * To support loadable plugins that modify planner behavior by changing the
 * join searching algorithm, we provide a hook variable that lets a plugin
 * replace or supplement this function.  Any such hook must return the same
 * final join relation as the standard code would, but it might have a
 * different set of implementation paths attached, and only the sub-joinrels
 * needed for these paths need have been instantiated.
 *
 * Note to plugin authors: the functions invoked during standard_join_search()
 * modify root->join_rel_list and root->join_rel_hash.  If you want to do more
 * than one join-order search, you'll probably need to save and restore the
 * original states of those data structures.  See geqo_eval() for an example.
 */
RelOptInfo *
standard_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	int			lev;
	RelOptInfo *rel;

	/*
	 * This function cannot be invoked recursively within any one planning
	 * problem, so join_rel_level[] can't be in use already.
	 */
	Assert(root->join_rel_level == NULL);

	/*
	 * We employ a simple "dynamic programming" algorithm: we first find all
	 * ways to build joins of two jointree items, then all ways to build joins
	 * of three items (from two-item joins and single items), then four-item
	 * joins, and so on until we have considered all ways to join all the
	 * items into one rel.
	 *
	 * root->join_rel_level[j] is a list of all the j-item rels.  Initially we
	 * set root->join_rel_level[1] to represent all the single-jointree-item
	 * relations.
	 */
	root->join_rel_level = (List **) palloc0((levels_needed + 1) * sizeof(List *));

	root->join_rel_level[1] = initial_rels;

	for (lev = 2; lev <= levels_needed; lev++)
	{
		ListCell   *lc;

		/*
		 * Determine all possible pairs of relations to be joined at this
		 * level, and build paths for making each one from every available
		 * pair of lower-level relations.
		 */
		join_search_one_level(root, lev);

		/*
		 * Run generate_gather_paths() for each just-processed joinrel.  We
		 * could not do this earlier because both regular and partial paths
		 * can get added to a particular joinrel at multiple times within
		 * join_search_one_level.  After that, we're done creating paths for
		 * the joinrel, so run set_cheapest().
		 */
		foreach(lc, root->join_rel_level[lev])
		{
			rel = (RelOptInfo *) lfirst(lc);

			/* Create GatherPaths for any useful partial paths for rel */
			generate_gather_paths(root, rel);

			/* Find and save the cheapest paths for this rel */
			set_cheapest(rel);

#ifdef OPTIMIZER_DEBUG
			debug_print_rel(root, rel);
#endif
		}
	}

	/*
	 * We should have a single rel at the final level.
	 */
	if (root->join_rel_level[levels_needed] == NIL)
		elog(ERROR, "failed to build any %d-way joins", levels_needed);
	Assert(list_length(root->join_rel_level[levels_needed]) == 1);

	rel = (RelOptInfo *) linitial(root->join_rel_level[levels_needed]);

	root->join_rel_level = NULL;

	return rel;
}

/*****************************************************************************
 *			PUSHING QUALS DOWN INTO SUBQUERIES
 *****************************************************************************/

/*
 * subquery_is_pushdown_safe - is a subquery safe for pushing down quals?
 *
 * subquery is the particular component query being checked.  topquery
 * is the top component of a set-operations tree (the same Query if no
 * set-op is involved).
 *
 * Conditions checked here:
 *
 * 1. If the subquery has a LIMIT clause, we must not push down any quals,
 * since that could change the set of rows returned.
 *
 * 2. If the subquery contains EXCEPT or EXCEPT ALL set ops we cannot push
 * quals into it, because that could change the results.
 *
 * 3. If the subquery uses DISTINCT, we cannot push volatile quals into it.
 * This is because upper-level quals should semantically be evaluated only
 * once per distinct row, not once per original row, and if the qual is
 * volatile then extra evaluations could change the results.  (This issue
 * does not apply to other forms of aggregation such as GROUP BY, because
 * when those are present we push into HAVING not WHERE, so that the quals
 * are still applied after aggregation.)
 *
 * 4. If the subquery contains window functions, we cannot push volatile quals
 * into it.  The issue here is a bit different from DISTINCT: a volatile qual
 * might succeed for some rows of a window partition and fail for others,
 * thereby changing the partition contents and thus the window functions'
 * results for rows that remain.
 *
 * 5. If the subquery contains any set-returning functions in its targetlist,
 * we cannot push volatile quals into it.  That would push them below the SRFs
 * and thereby change the number of times they are evaluated.  Also, a
 * volatile qual could succeed for some SRF output rows and fail for others,
 * a behavior that cannot occur if it's evaluated before SRF expansion.
 *
 * In addition, we make several checks on the subquery's output columns to see
 * if it is safe to reference them in pushed-down quals.  If output column k
 * is found to be unsafe to reference, we set safetyInfo->unsafeColumns[k]
 * to TRUE, but we don't reject the subquery overall since column k might not
 * be referenced by some/all quals.  The unsafeColumns[] array will be
 * consulted later by qual_is_pushdown_safe().  It's better to do it this way
 * than to make the checks directly in qual_is_pushdown_safe(), because when
 * the subquery involves set operations we have to check the output
 * expressions in each arm of the set op.
 *
 * Note: pushing quals into a DISTINCT subquery is theoretically dubious:
 * we're effectively assuming that the quals cannot distinguish values that
 * the DISTINCT's equality operator sees as equal, yet there are many
 * counterexamples to that assumption.  However use of such a qual with a
 * DISTINCT subquery would be unsafe anyway, since there's no guarantee which
 * "equal" value will be chosen as the output value by the DISTINCT operation.
 * So we don't worry too much about that.  Another objection is that if the
 * qual is expensive to evaluate, running it for each original row might cost
 * more than we save by eliminating rows before the DISTINCT step.  But it
 * would be very hard to estimate that at this stage, and in practice pushdown
 * seldom seems to make things worse, so we ignore that problem too.
 *
 * Note: likewise, pushing quals into a subquery with window functions is a
 * bit dubious: the quals might remove some rows of a window partition while
 * leaving others, causing changes in the window functions' results for the
 * surviving rows.  We insist that such a qual reference only partitioning
 * columns, but again that only protects us if the qual does not distinguish
 * values that the partitioning equality operator sees as equal.  The risks
 * here are perhaps larger than for DISTINCT, since no de-duplication of rows
 * occurs and thus there is no theoretical problem with such a qual.  But
 * we'll do this anyway because the potential performance benefits are very
 * large, and we've seen no field complaints about the longstanding comparable
 * behavior with DISTINCT.
 */
static bool
subquery_is_pushdown_safe(Query *subquery, Query *topquery,
						  pushdown_safety_info *safetyInfo)
{
	SetOperationStmt *topop;

	/* Check point 1 */
	if (subquery->limitOffset != NULL || subquery->limitCount != NULL)
		return false;

	/* Check points 3, 4, and 5 */
	if (subquery->distinctClause ||
		subquery->hasWindowFuncs ||
		subquery->hasTargetSRFs)
		safetyInfo->unsafeVolatile = true;

	/*
	 * If we're at a leaf query, check for unsafe expressions in its target
	 * list, and mark any unsafe ones in unsafeColumns[].  (Non-leaf nodes in
	 * setop trees have only simple Vars in their tlists, so no need to check
	 * them.)
	 */
	if (subquery->setOperations == NULL)
		check_output_expressions(subquery, safetyInfo);

	/* Are we at top level, or looking at a setop component? */
	if (subquery == topquery)
	{
		/* Top level, so check any component queries */
		if (subquery->setOperations != NULL)
			if (!recurse_pushdown_safe(subquery->setOperations, topquery,
									   safetyInfo))
				return false;
	}
	else
	{
		/* Setop component must not have more components (too weird) */
		if (subquery->setOperations != NULL)
			return false;
		/* Check whether setop component output types match top level */
		topop = castNode(SetOperationStmt, topquery->setOperations);
		Assert(topop);
		compare_tlist_datatypes(subquery->targetList,
								topop->colTypes,
								safetyInfo);
	}
	return true;
}

/*
 * Helper routine to recurse through setOperations tree
 */
static bool
recurse_pushdown_safe(Node *setOp, Query *topquery,
					  pushdown_safety_info *safetyInfo)
{
	if (IsA(setOp, RangeTblRef))
	{
		RangeTblRef *rtr = (RangeTblRef *) setOp;
		RangeTblEntry *rte = rt_fetch(rtr->rtindex, topquery->rtable);
		Query	   *subquery = rte->subquery;

		Assert(subquery != NULL);
		return subquery_is_pushdown_safe(subquery, topquery, safetyInfo);
	}
	else if (IsA(setOp, SetOperationStmt))
	{
		SetOperationStmt *op = (SetOperationStmt *) setOp;

		/* EXCEPT is no good (point 2 for subquery_is_pushdown_safe) */
		if (op->op == SETOP_EXCEPT)
			return false;
		/* Else recurse */
		if (!recurse_pushdown_safe(op->larg, topquery, safetyInfo))
			return false;
		if (!recurse_pushdown_safe(op->rarg, topquery, safetyInfo))
			return false;
	}
	else
	{
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(setOp));
	}
	return true;
}

/*
 * check_output_expressions - check subquery's output expressions for safety
 *
 * There are several cases in which it's unsafe to push down an upper-level
 * qual if it references a particular output column of a subquery.  We check
 * each output column of the subquery and set unsafeColumns[k] to TRUE if
 * that column is unsafe for a pushed-down qual to reference.  The conditions
 * checked here are:
 *
 * 1. We must not push down any quals that refer to subselect outputs that
 * return sets, else we'd introduce functions-returning-sets into the
 * subquery's WHERE/HAVING quals.
 *
 * 2. We must not push down any quals that refer to subselect outputs that
 * contain volatile functions, for fear of introducing strange results due
 * to multiple evaluation of a volatile function.
 *
 * 3. If the subquery uses DISTINCT ON, we must not push down any quals that
 * refer to non-DISTINCT output columns, because that could change the set
 * of rows returned.  (This condition is vacuous for DISTINCT, because then
 * there are no non-DISTINCT output columns, so we needn't check.  Note that
 * subquery_is_pushdown_safe already reported that we can't use volatile
 * quals if there's DISTINCT or DISTINCT ON.)
 *
 * 4. If the subquery has any window functions, we must not push down quals
 * that reference any output columns that are not listed in all the subquery's
 * window PARTITION BY clauses.  We can push down quals that use only
 * partitioning columns because they should succeed or fail identically for
 * every row of any one window partition, and totally excluding some
 * partitions will not change a window function's results for remaining
 * partitions.  (Again, this also requires nonvolatile quals, but
 * subquery_is_pushdown_safe handles that.)
 */
static void
check_output_expressions(Query *subquery, pushdown_safety_info *safetyInfo)
{
	ListCell   *lc;

	foreach(lc, subquery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);

		if (tle->resjunk)
			continue;			/* ignore resjunk columns */

		/* We need not check further if output col is already known unsafe */
		if (safetyInfo->unsafeColumns[tle->resno])
			continue;

		/* Functions returning sets are unsafe (point 1) */
		if (subquery->hasTargetSRFs &&
			expression_returns_set((Node *) tle->expr))
		{
			safetyInfo->unsafeColumns[tle->resno] = true;
			continue;
		}

		/* Volatile functions are unsafe (point 2) */
		if (contain_volatile_functions((Node *) tle->expr))
		{
			safetyInfo->unsafeColumns[tle->resno] = true;
			continue;
		}

		/* If subquery uses DISTINCT ON, check point 3 */
		if (subquery->hasDistinctOn &&
			!targetIsInSortList(tle, InvalidOid, subquery->distinctClause))
		{
			/* non-DISTINCT column, so mark it unsafe */
			safetyInfo->unsafeColumns[tle->resno] = true;
			continue;
		}

		/* If subquery uses window functions, check point 4 */
		if (subquery->hasWindowFuncs &&
			!targetIsInAllPartitionLists(tle, subquery))
		{
			/* not present in all PARTITION BY clauses, so mark it unsafe */
			safetyInfo->unsafeColumns[tle->resno] = true;
			continue;
		}
	}
}

/*
 * For subqueries using UNION/UNION ALL/INTERSECT/INTERSECT ALL, we can
 * push quals into each component query, but the quals can only reference
 * subquery columns that suffer no type coercions in the set operation.
 * Otherwise there are possible semantic gotchas.  So, we check the
 * component queries to see if any of them have output types different from
 * the top-level setop outputs.  unsafeColumns[k] is set true if column k
 * has different type in any component.
 *
 * We don't have to care about typmods here: the only allowed difference
 * between set-op input and output typmods is input is a specific typmod
 * and output is -1, and that does not require a coercion.
 *
 * tlist is a subquery tlist.
 * colTypes is an OID list of the top-level setop's output column types.
 * safetyInfo->unsafeColumns[] is the result array.
 */
static void
compare_tlist_datatypes(List *tlist, List *colTypes,
						pushdown_safety_info *safetyInfo)
{
	ListCell   *l;
	ListCell   *colType = list_head(colTypes);

	foreach(l, tlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);

		if (tle->resjunk)
			continue;			/* ignore resjunk columns */
		if (colType == NULL)
			elog(ERROR, "wrong number of tlist entries");
		if (exprType((Node *) tle->expr) != lfirst_oid(colType))
			safetyInfo->unsafeColumns[tle->resno] = true;
		colType = lnext(colType);
	}
	if (colType != NULL)
		elog(ERROR, "wrong number of tlist entries");
}

/*
 * targetIsInAllPartitionLists
 *		True if the TargetEntry is listed in the PARTITION BY clause
 *		of every window defined in the query.
 *
 * It would be safe to ignore windows not actually used by any window
 * function, but it's not easy to get that info at this stage; and it's
 * unlikely to be useful to spend any extra cycles getting it, since
 * unreferenced window definitions are probably infrequent in practice.
 */
static bool
targetIsInAllPartitionLists(TargetEntry *tle, Query *query)
{
	ListCell   *lc;

	foreach(lc, query->windowClause)
	{
		WindowClause *wc = (WindowClause *) lfirst(lc);

		if (!targetIsInSortList(tle, InvalidOid, wc->partitionClause))
			return false;
	}
	return true;
}

/*
 * qual_is_pushdown_safe - is a particular qual safe to push down?
 *
 * qual is a restriction clause applying to the given subquery (whose RTE
 * has index rti in the parent query).
 *
 * Conditions checked here:
 *
 * 1. The qual must not contain any subselects (mainly because I'm not sure
 * it will work correctly: sublinks will already have been transformed into
 * subplans in the qual, but not in the subquery).
 *
 * 2. If unsafeVolatile is set, the qual must not contain any volatile
 * functions.
 *
 * 3. If unsafeLeaky is set, the qual must not contain any leaky functions
 * that are passed Var nodes, and therefore might reveal values from the
 * subquery as side effects.
 *
 * 4. The qual must not refer to the whole-row output of the subquery
 * (since there is no easy way to name that within the subquery itself).
 *
 * 5. The qual must not refer to any subquery output columns that were
 * found to be unsafe to reference by subquery_is_pushdown_safe().
 */
static bool
qual_is_pushdown_safe(Query *subquery, Index rti, Node *qual,
					  pushdown_safety_info *safetyInfo)
{
	bool		safe = true;
	List	   *vars;
	ListCell   *vl;

	/* Refuse subselects (point 1) */
	if (contain_subplans(qual))
		return false;

	/* Refuse volatile quals if we found they'd be unsafe (point 2) */
	if (safetyInfo->unsafeVolatile &&
		contain_volatile_functions(qual))
		return false;

	/* Refuse leaky quals if told to (point 3) */
	if (safetyInfo->unsafeLeaky &&
		contain_leaked_vars(qual))
		return false;

	/*
	 * It would be unsafe to push down window function calls, but at least for
	 * the moment we could never see any in a qual anyhow.  (The same applies
	 * to aggregates, which we check for in pull_var_clause below.)
	 */
	Assert(!contain_window_function(qual));

	/*
	 * Examine all Vars used in clause; since it's a restriction clause, all
	 * such Vars must refer to subselect output columns.
	 */
	vars = pull_var_clause(qual, PVC_INCLUDE_PLACEHOLDERS);
	foreach(vl, vars)
	{
		Var		   *var = (Var *) lfirst(vl);

		/*
		 * XXX Punt if we find any PlaceHolderVars in the restriction clause.
		 * It's not clear whether a PHV could safely be pushed down, and even
		 * less clear whether such a situation could arise in any cases of
		 * practical interest anyway.  So for the moment, just refuse to push
		 * down.
		 */
		if (!IsA(var, Var))
		{
			safe = false;
			break;
		}

		Assert(var->varno == rti);
		Assert(var->varattno >= 0);

		/* Check point 4 */
		if (var->varattno == 0)
		{
			safe = false;
			break;
		}

		/* Check point 5 */
		if (safetyInfo->unsafeColumns[var->varattno])
		{
			safe = false;
			break;
		}
	}

	list_free(vars);

	return safe;
}

/*
 * subquery_push_qual - push down a qual that we have determined is safe
 */
static void
subquery_push_qual(Query *subquery, RangeTblEntry *rte, Index rti, Node *qual)
{
	if (subquery->setOperations != NULL)
	{
		/* Recurse to push it separately to each component query */
		recurse_push_qual(subquery->setOperations, subquery,
						  rte, rti, qual);
	}
	else
	{
		/*
		 * We need to replace Vars in the qual (which must refer to outputs of
		 * the subquery) with copies of the subquery's targetlist expressions.
		 * Note that at this point, any uplevel Vars in the qual should have
		 * been replaced with Params, so they need no work.
		 *
		 * This step also ensures that when we are pushing into a setop tree,
		 * each component query gets its own copy of the qual.
		 */
		qual = ReplaceVarsFromTargetList(qual, rti, 0, rte,
										 subquery->targetList,
										 REPLACEVARS_REPORT_ERROR, 0,
										 &subquery->hasSubLinks);

		/*
		 * Now attach the qual to the proper place: normally WHERE, but if the
		 * subquery uses grouping or aggregation, put it in HAVING (since the
		 * qual really refers to the group-result rows).
		 */
		if (subquery->hasAggs || subquery->groupClause || subquery->groupingSets || subquery->havingQual)
			subquery->havingQual = make_and_qual(subquery->havingQual, qual);
		else
			subquery->jointree->quals =
				make_and_qual(subquery->jointree->quals, qual);

		/*
		 * We need not change the subquery's hasAggs or hasSubLinks flags,
		 * since we can't be pushing down any aggregates that weren't there
		 * before, and we don't push down subselects at all.
		 */
	}
}

/*
 * Helper routine to recurse through setOperations tree
 */
static void
recurse_push_qual(Node *setOp, Query *topquery,
				  RangeTblEntry *rte, Index rti, Node *qual)
{
	if (IsA(setOp, RangeTblRef))
	{
		RangeTblRef *rtr = (RangeTblRef *) setOp;
		RangeTblEntry *subrte = rt_fetch(rtr->rtindex, topquery->rtable);
		Query	   *subquery = subrte->subquery;

		Assert(subquery != NULL);
		subquery_push_qual(subquery, rte, rti, qual);
	}
	else if (IsA(setOp, SetOperationStmt))
	{
		SetOperationStmt *op = (SetOperationStmt *) setOp;

		recurse_push_qual(op->larg, topquery, rte, rti, qual);
		recurse_push_qual(op->rarg, topquery, rte, rti, qual);
	}
	else
	{
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(setOp));
	}
}

/*****************************************************************************
 *			SIMPLIFYING SUBQUERY TARGETLISTS
 *****************************************************************************/

/*
 * remove_unused_subquery_outputs
 *		Remove subquery targetlist items we don't need
 *
 * It's possible, even likely, that the upper query does not read all the
 * output columns of the subquery.  We can remove any such outputs that are
 * not needed by the subquery itself (e.g., as sort/group columns) and do not
 * affect semantics otherwise (e.g., volatile functions can't be removed).
 * This is useful not only because we might be able to remove expensive-to-
 * compute expressions, but because deletion of output columns might allow
 * optimizations such as join removal to occur within the subquery.
 *
 * To avoid affecting column numbering in the targetlist, we don't physically
 * remove unused tlist entries, but rather replace their expressions with NULL
 * constants.  This is implemented by modifying subquery->targetList.
 */
static void
remove_unused_subquery_outputs(Query *subquery, RelOptInfo *rel)
{
	Bitmapset  *attrs_used = NULL;
	ListCell   *lc;

	/*
	 * Do nothing if subquery has UNION/INTERSECT/EXCEPT: in principle we
	 * could update all the child SELECTs' tlists, but it seems not worth the
	 * trouble presently.
	 */
	if (subquery->setOperations)
		return;

	/*
	 * If subquery has regular DISTINCT (not DISTINCT ON), we're wasting our
	 * time: all its output columns must be used in the distinctClause.
	 */
	if (subquery->distinctClause && !subquery->hasDistinctOn)
		return;

	/*
	 * Collect a bitmap of all the output column numbers used by the upper
	 * query.
	 *
	 * Add all the attributes needed for joins or final output.  Note: we must
	 * look at rel's targetlist, not the attr_needed data, because attr_needed
	 * isn't computed for inheritance child rels, cf set_append_rel_size().
	 * (XXX might be worth changing that sometime.)
	 */
	pull_varattnos((Node *) rel->reltarget->exprs, rel->relid, &attrs_used);

	/* Add all the attributes used by un-pushed-down restriction clauses. */
	foreach(lc, rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		pull_varattnos((Node *) rinfo->clause, rel->relid, &attrs_used);
	}

	/*
	 * If there's a whole-row reference to the subquery, we can't remove
	 * anything.
	 */
	if (bms_is_member(0 - FirstLowInvalidHeapAttributeNumber, attrs_used))
		return;

	/*
	 * Run through the tlist and zap entries we don't need.  It's okay to
	 * modify the tlist items in-place because set_subquery_pathlist made a
	 * copy of the subquery.
	 */
	foreach(lc, subquery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);
		Node	   *texpr = (Node *) tle->expr;

		/*
		 * If it has a sortgroupref number, it's used in some sort/group
		 * clause so we'd better not remove it.  Also, don't remove any
		 * resjunk columns, since their reason for being has nothing to do
		 * with anybody reading the subquery's output.  (It's likely that
		 * resjunk columns in a sub-SELECT would always have ressortgroupref
		 * set, but even if they don't, it seems imprudent to remove them.)
		 */
		if (tle->ressortgroupref || tle->resjunk)
			continue;

		/*
		 * If it's used by the upper query, we can't remove it.
		 */
		if (bms_is_member(tle->resno - FirstLowInvalidHeapAttributeNumber,
						  attrs_used))
			continue;

		/*
		 * If it contains a set-returning function, we can't remove it since
		 * that could change the number of rows returned by the subquery.
		 */
		if (subquery->hasTargetSRFs &&
			expression_returns_set(texpr))
			continue;

		/*
		 * If it contains volatile functions, we daren't remove it for fear
		 * that the user is expecting their side-effects to happen.
		 */
		if (contain_volatile_functions(texpr))
			continue;

		/*
		 * OK, we don't need it.  Replace the expression with a NULL constant.
		 * Preserve the exposed type of the expression, in case something
		 * looks at the rowtype of the subquery's result.
		 */
		tle->expr = (Expr *) makeNullConst(exprType(texpr),
										   exprTypmod(texpr),
										   exprCollation(texpr));
	}
}

/*
 * create_partial_bitmap_paths
 *	  Build partial bitmap heap path for the relation
 */
void
create_partial_bitmap_paths(PlannerInfo *root, RelOptInfo *rel,
							Path *bitmapqual)
{
	int			parallel_workers;
	double		pages_fetched;

	/* Compute heap pages for bitmap heap scan */
	pages_fetched = compute_bitmap_pages(root, rel, bitmapqual, 1.0,
										 NULL, NULL);

	parallel_workers = compute_parallel_worker(rel, pages_fetched, -1);

	if (parallel_workers <= 0)
		return;

	add_partial_path(rel, (Path *) create_bitmap_heap_path(root, rel,
														   bitmapqual, rel->lateral_relids, 1.0, parallel_workers));
}

/*
 * Compute the number of parallel workers that should be used to scan a
 * relation.  We compute the parallel workers based on the size of the heap to
 * be scanned and the size of the index to be scanned, then choose a minimum
 * of those.
 *
 * "heap_pages" is the number of pages from the table that we expect to scan, or
 * -1 if we don't expect to scan any.
 *
 * "index_pages" is the number of pages from the index that we expect to scan, or
 * -1 if we don't expect to scan any.
 */
int
compute_parallel_worker(RelOptInfo *rel, double heap_pages, double index_pages)
{
	int			parallel_workers = 0;

	/*
	 * If the user has set the parallel_workers reloption, use that; otherwise
	 * select a default number of workers.
	 */
	if (rel->rel_parallel_workers != -1)
		parallel_workers = rel->rel_parallel_workers;
	else
	{
		/*
		 * If the number of pages being scanned is insufficient to justify a
		 * parallel scan, just return zero ... unless it's an inheritance
		 * child. In that case, we want to generate a parallel path here
		 * anyway.  It might not be worthwhile just for this relation, but
		 * when combined with all of its inheritance siblings it may well pay
		 * off.
		 */
		if (rel->reloptkind == RELOPT_BASEREL &&
			((heap_pages >= 0 && heap_pages < min_parallel_table_scan_size) ||
			 (index_pages >= 0 && index_pages < min_parallel_index_scan_size)))
			return 0;

		if (heap_pages >= 0)
		{
			int			heap_parallel_threshold;
			int			heap_parallel_workers = 1;

			/*
			 * Select the number of workers based on the log of the size of
			 * the relation.  This probably needs to be a good deal more
			 * sophisticated, but we need something here for now.  Note that
			 * the upper limit of the min_parallel_table_scan_size GUC is
			 * chosen to prevent overflow here.
			 */
			heap_parallel_threshold = Max(min_parallel_table_scan_size, 1);
			while (heap_pages >= (BlockNumber) (heap_parallel_threshold * 3))
			{
				heap_parallel_workers++;
				heap_parallel_threshold *= 3;
				if (heap_parallel_threshold > INT_MAX / 3)
					break;		/* avoid overflow */
			}

			parallel_workers = heap_parallel_workers;
		}

		if (index_pages >= 0)
		{
			int			index_parallel_workers = 1;
			int			index_parallel_threshold;

			/* same calculation as for heap_pages above */
			index_parallel_threshold = Max(min_parallel_index_scan_size, 1);
			while (index_pages >= (BlockNumber) (index_parallel_threshold * 3))
			{
				index_parallel_workers++;
				index_parallel_threshold *= 3;
				if (index_parallel_threshold > INT_MAX / 3)
					break;		/* avoid overflow */
			}

			if (parallel_workers > 0)
				parallel_workers = Min(parallel_workers, index_parallel_workers);
			else
				parallel_workers = index_parallel_workers;
		}
	}

	/*
	 * In no case use more than max_parallel_workers_per_gather workers.
	 */
	parallel_workers = Min(parallel_workers, max_parallel_workers_per_gather);

	return parallel_workers;
}

#ifdef ADB
static bool set_path_reduce_info_worker(Path *path, List *reduce_info_list)
{
	if (path != NULL)
	{
		if(path->reduce_is_valid == false)
		{
			path->reduce_info_list = reduce_info_list;
			path->reduce_is_valid = true;
		}
		return path_tree_walker(path, set_path_reduce_info_worker, reduce_info_list);
	}
	return false;
}

static bool get_subplan_ref_walker(Expr *expr, int *subplan_ref)
{
	if (expr == NULL)
		return false;
	if (IsA(expr, SubPlan))
	{
		SubPlan *sub = (SubPlan*)expr;
		subplan_ref[sub->plan_id-1]++;
		return false;
	}else if (IsA(expr, RestrictInfo))
	{
		return expression_tree_walker((Node*)((RestrictInfo*)expr)->clause,
									  get_subplan_ref_walker,
									  subplan_ref);
	}
	return expression_tree_walker((Node*)expr, get_subplan_ref_walker, subplan_ref);
}

static int get_max_parallel_workers(List *pathlist)
{
	ListCell *lc;
	Path *path;
	int max_parallel_workers = 0;

	foreach(lc, pathlist)
	{
		path = lfirst(lc);
		if (max_parallel_workers < path->parallel_workers)
			max_parallel_workers = path->parallel_workers;
	}

	return max_parallel_workers;
}
#endif /* ADB */

/*****************************************************************************
 *			DEBUG SUPPORT
 *****************************************************************************/

#ifdef OPTIMIZER_DEBUG

static void
print_relids(PlannerInfo *root, Relids relids)
{
	int			x;
	bool		first = true;

	x = -1;
	while ((x = bms_next_member(relids, x)) >= 0)
	{
		if (!first)
			printf(" ");
		if (x < root->simple_rel_array_size &&
			root->simple_rte_array[x])
			printf("%s", root->simple_rte_array[x]->eref->aliasname);
		else
			printf("%d", x);
		first = false;
	}
}

static void
print_restrictclauses(PlannerInfo *root, List *clauses)
{
	ListCell   *l;

	foreach(l, clauses)
	{
		RestrictInfo *c = lfirst(l);

		print_expr((Node *) c->clause, root->parse->rtable);
		if (lnext(l))
			printf(", ");
	}
}

static void
print_path(PlannerInfo *root, Path *path, int indent)
{
	const char *ptype;
	bool		join = false;
	Path	   *subpath = NULL;
	int			i;

	switch (nodeTag(path))
	{
		case T_Path:
			switch (path->pathtype)
			{
				case T_SeqScan:
					ptype = "SeqScan";
					break;
				case T_SampleScan:
					ptype = "SampleScan";
					break;
				case T_SubqueryScan:
					ptype = "SubqueryScan";
					break;
				case T_FunctionScan:
					ptype = "FunctionScan";
					break;
				case T_TableFuncScan:
					ptype = "TableFuncScan";
					break;
				case T_ValuesScan:
					ptype = "ValuesScan";
					break;
				case T_CteScan:
					ptype = "CteScan";
					break;
				case T_WorkTableScan:
					ptype = "WorkTableScan";
					break;
				default:
					ptype = "???Path";
					break;
			}
			break;
		case T_IndexPath:
			ptype = "IdxScan";
			break;
		case T_BitmapHeapPath:
			ptype = "BitmapHeapScan";
			break;
		case T_BitmapAndPath:
			ptype = "BitmapAndPath";
			break;
		case T_BitmapOrPath:
			ptype = "BitmapOrPath";
			break;
		case T_TidPath:
			ptype = "TidScan";
			break;
		case T_SubqueryScanPath:
			ptype = "SubqueryScanScan";
			break;
		case T_ForeignPath:
			ptype = "ForeignScan";
			break;
		case T_AppendPath:
			ptype = "Append";
			break;
		case T_MergeAppendPath:
			ptype = "MergeAppend";
			break;
		case T_ResultPath:
			ptype = "Result";
			break;
		case T_MaterialPath:
			ptype = "Material";
			subpath = ((MaterialPath *) path)->subpath;
			break;
		case T_UniquePath:
			ptype = "Unique";
			subpath = ((UniquePath *) path)->subpath;
			break;
		case T_GatherPath:
			ptype = "Gather";
			subpath = ((GatherPath *) path)->subpath;
			break;
		case T_ProjectionPath:
			ptype = "Projection";
			subpath = ((ProjectionPath *) path)->subpath;
			break;
		case T_ProjectSetPath:
			ptype = "ProjectSet";
			subpath = ((ProjectSetPath *) path)->subpath;
			break;
		case T_SortPath:
			ptype = "Sort";
			subpath = ((SortPath *) path)->subpath;
			break;
		case T_GroupPath:
			ptype = "Group";
			subpath = ((GroupPath *) path)->subpath;
			break;
		case T_UpperUniquePath:
			ptype = "UpperUnique";
			subpath = ((UpperUniquePath *) path)->subpath;
			break;
		case T_AggPath:
			ptype = "Agg";
			subpath = ((AggPath *) path)->subpath;
			break;
		case T_GroupingSetsPath:
			ptype = "GroupingSets";
			subpath = ((GroupingSetsPath *) path)->subpath;
			break;
		case T_MinMaxAggPath:
			ptype = "MinMaxAgg";
			break;
		case T_WindowAggPath:
			ptype = "WindowAgg";
			subpath = ((WindowAggPath *) path)->subpath;
			break;
		case T_SetOpPath:
			ptype = "SetOp";
			subpath = ((SetOpPath *) path)->subpath;
			break;
		case T_RecursiveUnionPath:
			ptype = "RecursiveUnion";
			break;
		case T_LockRowsPath:
			ptype = "LockRows";
			subpath = ((LockRowsPath *) path)->subpath;
			break;
		case T_ModifyTablePath:
			ptype = "ModifyTable";
			break;
		case T_LimitPath:
			ptype = "Limit";
			subpath = ((LimitPath *) path)->subpath;
			break;
		case T_NestPath:
			ptype = "NestLoop";
			join = true;
			break;
		case T_MergePath:
			ptype = "MergeJoin";
			join = true;
			break;
		case T_HashPath:
			ptype = "HashJoin";
			join = true;
			break;
#ifdef ADB
		case T_RemoteQueryPath:
			ptype = "RemoteQuery";
			break;
		case T_ClusterScanPath:
			ptype = "ClusterScan";
			subpath = ((ClusterScanPath*) path)->cluster_path.subpath;
			break;
		case T_ClusterGatherPath:
			ptype = "ClusterGather";
			subpath = ((ClusterGatherPath*) path)->subpath;
			break;
		case T_ClusterMergeGatherPath:
			ptype = "ClusterMergeGather";
			subpath = ((ClusterMergeGatherPath*) path)->subpath;
			break;
#endif /* ADB */
		default:
			ptype = "???Path";
			break;
	}

	for (i = 0; i < indent; i++)
		printf("\t");
	printf("%s", ptype);

	if (path->parent)
	{
		printf("(");
		print_relids(root, path->parent->relids);
		printf(")");
	}
	if (path->param_info)
	{
		printf(" required_outer (");
		print_relids(root, path->param_info->ppi_req_outer);
		printf(")");
	}
	printf(" rows=%.0f cost=%.2f..%.2f\n",
		   path->rows, path->startup_cost, path->total_cost);

	if (path->pathkeys)
	{
		for (i = 0; i < indent; i++)
			printf("\t");
		printf("  pathkeys: ");
		print_pathkeys(path->pathkeys, root->parse->rtable);
	}

	if (join)
	{
		JoinPath   *jp = (JoinPath *) path;

		for (i = 0; i < indent; i++)
			printf("\t");
		printf("  clauses: ");
		print_restrictclauses(root, jp->joinrestrictinfo);
		printf("\n");

		if (IsA(path, MergePath))
		{
			MergePath  *mp = (MergePath *) path;

			for (i = 0; i < indent; i++)
				printf("\t");
			printf("  sortouter=%d sortinner=%d materializeinner=%d\n",
				   ((mp->outersortkeys) ? 1 : 0),
				   ((mp->innersortkeys) ? 1 : 0),
				   ((mp->materialize_inner) ? 1 : 0));
		}

		print_path(root, jp->outerjoinpath, indent + 1);
		print_path(root, jp->innerjoinpath, indent + 1);
	}

	if (subpath)
		print_path(root, subpath, indent + 1);
}

void
debug_print_rel(PlannerInfo *root, RelOptInfo *rel)
{
	ListCell   *l;

	printf("RELOPTINFO (");
	print_relids(root, rel->relids);
	printf("): rows=%.0f width=%d\n", rel->rows, rel->reltarget->width);

	if (rel->baserestrictinfo)
	{
		printf("\tbaserestrictinfo: ");
		print_restrictclauses(root, rel->baserestrictinfo);
		printf("\n");
	}

	if (rel->joininfo)
	{
		printf("\tjoininfo: ");
		print_restrictclauses(root, rel->joininfo);
		printf("\n");
	}

	printf("\tpath list:\n");
	foreach(l, rel->pathlist)
		print_path(root, lfirst(l), 1);
	if (rel->cheapest_parameterized_paths)
	{
		printf("\n\tcheapest parameterized paths:\n");
		foreach(l, rel->cheapest_parameterized_paths)
			print_path(root, lfirst(l), 1);
	}
	if (rel->cheapest_startup_path)
	{
		printf("\n\tcheapest startup path:\n");
		print_path(root, rel->cheapest_startup_path, 1);
	}
	if (rel->cheapest_total_path)
	{
		printf("\n\tcheapest total path:\n");
		print_path(root, rel->cheapest_total_path, 1);
	}
	printf("\n");
#ifdef ADB
	printf("\tcluster path list:\n");
	foreach(l, rel->cluster_pathlist)
		print_path(root, lfirst(l), 1);
	printf("\n");
#endif /* ADB */
	fflush(stdout);
}

#endif							/* OPTIMIZER_DEBUG */
