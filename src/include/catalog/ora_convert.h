#ifndef ORA_CONVERT_H
#define ORA_CONVERT_H

/* oracle grammar implicit convert */

#include "catalog/genbki.h"
#include "catalog/ora_convert_d.h"

CATALOG(ora_convert,9316,OraConvertRelationId)
{
	/* convert id */
	Oid			cvtid;

	/* implicit convert kind, ORA_CONVERT_KIND_XXX */
	char		cvtkind;

	NameData	cvtname;

	oidvector	cvtfrom BKI_LOOKUP(pg_type);

#ifdef CATALOG_VARLEN
	oidvector	cvtto BKI_LOOKUP(pg_type);
#endif
} FormData_ora_convert;

typedef FormData_ora_convert *Form_ora_convert;

#ifdef EXPOSE_TO_CLIENT_CODE

#define ORA_CONVERT_KIND_OPERATOR	'o'
#define ORA_CONVERT_KIND_FUNCTION	'f'
/*
 * common for combin decode,nvl2... functions
 * and UNION INTERSECT, MINUS Operators
 * cvtname is empty
 */
#define ORA_CONVERT_KIND_COMMON		'c'
/*
 * special functions
 * decode(arg, cmp1, ret1, cmp2, ret2, def) check ('s', 'decode', 'ret1 ret2')
 * 										 => check ('s', 'decode', 'pret def')
 */
#define ORA_CONVERT_KIND_SPECIAL_FUN 's'

#endif							/* EXPOSE_TO_CLIENT_CODE */

#endif