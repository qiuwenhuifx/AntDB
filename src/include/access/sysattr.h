/*-------------------------------------------------------------------------
 *
 * sysattr.h
 *	  POSTGRES system attribute definitions.
 *
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/sysattr.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SYSATTR_H
#define SYSATTR_H


/*
 * Attribute numbers for the system-defined attributes
 */
#define SelfItemPointerAttributeNumber			(-1)
#define ObjectIdAttributeNumber					(-2)
#define MinTransactionIdAttributeNumber			(-3)
#define MinCommandIdAttributeNumber				(-4)
#define MaxTransactionIdAttributeNumber			(-5)
#define MaxCommandIdAttributeNumber				(-6)
#define TableOidAttributeNumber					(-7)
#ifdef ADB
	#define XC_NodeIdAttributeNumber				(-8)
	#ifdef ADB_GRAM_ORA
		#define ADB_RowIdAttributeNumber			(-9)
		#define ADB_InfoMaskAttributeNumber			(-10)
		#define FirstLowInvalidHeapAttributeNumber	(-11)
	#else /* ADB_GRAM_ORA */
		#define ADB_InfoMaskAttributeNumber			(-9)
		#define FirstLowInvalidHeapAttributeNumber	(-10)
	#endif /* ADB_GRAM_ORA */
#elif defined(ADB_GRAM_ORA) /* else adb */
	#define ADB_RowIdAttributeNumber				(-8)
	#define ADB_InfoMaskAttributeNumber				(-9)
	#define FirstLowInvalidHeapAttributeNumber		(-10)
#else /* else ADB, else ADB_GRAM_ORA */
	#define FirstLowInvalidHeapAttributeNumber		(-8)
#endif

#endif							/* SYSATTR_H */
