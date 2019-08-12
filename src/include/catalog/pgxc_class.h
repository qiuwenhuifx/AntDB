/*-----------------------------------------------------------
 *
 * Portions Copyright (c) 2010-2013, Postgres-XC Development Group
 * Portions Copyright (c) 2014-2017, ADB Development Group
 *
 *-----------------------------------------------------------
 */
#ifndef PGXC_CLASS_H
#define PGXC_CLASS_H

#include "catalog/genbki.h"
#include "catalog/pgxc_class_d.h"

CATALOG(pgxc_class,9020,PgxcClassRelationId) BKI_WITHOUT_OIDS
{
	/* Table Oid */
	Oid			pcrelid;

	/* Type of distribution */
	char		pclocatortype;

	/* Column number of distribution */
	int16		pcattnum;

	/* Hashing algorithm */
	int16		pchashalgorithm;

	/* Number of buckets */
	int16		pchashbuckets;

	/* User-defined distribution function oid */
	Oid			pcfuncid;

	/* List of nodes used by table */
	oidvector	nodeoids;

#ifdef CATALOG_VARLEN

	/* List of column number of distribution */
	int2vector	pcfuncattnums;
#endif
} FormData_pgxc_class;

typedef FormData_pgxc_class *Form_pgxc_class;

#ifdef EXPOSE_TO_CLIENT_CODE

typedef enum PgxcClassAlterType
{
	PGXC_CLASS_ALTER_DISTRIBUTION,
	PGXC_CLASS_ALTER_NODES,
	PGXC_CLASS_ALTER_ALL
} PgxcClassAlterType;

#define LOCATOR_TYPE_REPLICATED		'R'
#define LOCATOR_TYPE_HASH			'H'
#define LOCATOR_TYPE_RANGE			'G'
#define LOCATOR_TYPE_RANDOM			'N'
#define LOCATOR_TYPE_CUSTOM			'C'
#define LOCATOR_TYPE_MODULO			'M'
#define LOCATOR_TYPE_NONE			'O'
#define LOCATOR_TYPE_DISTRIBUTED	'D'	/* for distributed table without specific
										 * scheme, e.g. result of JOIN of
										 * replicated and distributed table */
#define LOCATOR_TYPE_USER_DEFINED	'U'
#define LOCATOR_TYPE_HASHMAP		'B'


/* Maximum number of preferred Datanodes that can be defined in cluster */
#define MAX_PREFERRED_NODES 64

#define HASH_SIZE 4096
#define HASH_MASK 0x00000FFF;

#define IsLocatorNone(x)						((x) == LOCATOR_TYPE_NONE)
#define IsLocatorReplicated(x) 					((x) == LOCATOR_TYPE_REPLICATED)
#define IsLocatorColumnDistributed(x) 			((x) == LOCATOR_TYPE_HASH || \
												 (x) == LOCATOR_TYPE_RANDOM || \
												 (x) == LOCATOR_TYPE_HASHMAP || \
												 (x) == LOCATOR_TYPE_MODULO || \
												 (x) == LOCATOR_TYPE_DISTRIBUTED || \
												 (x) == LOCATOR_TYPE_USER_DEFINED)
#define IsLocatorDistributedByValue(x)			((x) == LOCATOR_TYPE_HASH || \
												 (x) == LOCATOR_TYPE_MODULO || \
												 (x) == LOCATOR_TYPE_HASHMAP || \
												 (x) == LOCATOR_TYPE_RANGE)
#define IsLocatorDistributedByUserDefined(x)	((x) == LOCATOR_TYPE_USER_DEFINED)

#define IsLocatorHashmap(x) 					(x == LOCATOR_TYPE_HASHMAP)

#endif							/* EXPOSE_TO_CLIENT_CODE */

extern void PgxcClassCreate(Oid pcrelid,
							char pclocatortype,
							int pcattnum,
							int pchashalgorithm,
							int pchashbuckets,
							int numnodes,
							Oid *nodes,
							Oid pcfuncid,
							int numatts,
							int16 *pcfuncattnums);
extern void PgxcClassAlter(Oid pcrelid,
						   char pclocatortype,
						   int pcattnum,
						   int pchashalgorithm,
						   int pchashbuckets,
						   int numnodes,
						   Oid *nodes,
						   PgxcClassAlterType type,
						   Oid pcfuncid,
						   int numatts,
						   int16 *pcfuncattnums);
extern void RemovePgxcClass(Oid pcrelid);

extern void CreatePgxcRelationFuncDepend(Oid relid, Oid funcid);
extern void CreatePgxcRelationAttrDepend(Oid relid, AttrNumber attnum);

#endif   /* PGXC_CLASS_H */
