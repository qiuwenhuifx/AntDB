#include "postgres.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/numeric.h"
#include "utils/pg_locale.h"
#include "utils/formatting.h"

#include "oraschema.h"

extern char *nls_date_format;
extern char *nls_timestamp_format;
extern char *nls_timestamp_tz_format;

static int getindex(const char **map, char *mbchar, int mblen);

#define RETURN_IF_WITHOUT_FORMAT(out_func) \
	do { \
		if (!fmt) \
		{ \
			Datum result; \
			result = DirectFunctionCall1(out_func, PG_GETARG_DATUM(0)); \
			PG_RETURN_TEXT_P(cstring_to_text(DatumGetCString(result))); \
		} \
	} while(0)

#define RETURN_NULL_IF_EMPTY_INPUT(in) \
	do { \
		if (in) \
		{ \
			char instr[2]= {0, 0}; \
			text_to_cstring_buffer(in, instr, 2); \
			if (instr[0] == '\0') \
				PG_RETURN_NULL(); \
		} \
	} while (0)

#define DECLARE_TO_CHAR_FMT(type, func)						\
Datum ora_##type##_tochar_fmt(PG_FUNCTION_ARGS)				\
{															\
	Pointer ptr = PG_GETARG_POINTER(1);						\
	if (likely(VARSIZE_ANY_EXHDR(ptr) > 0))					\
		return func(fcinfo);								\
	PG_RETURN_NULL();										\
}extern int not_exists

#define DECLARE_NUM_TO_CHAR(type, out_fun, to_char_fun)		\
Datum ora_##type##_tochar(PG_FUNCTION_ARGS)					\
{															\
	char *str = DatumGetCString(out_fun(fcinfo));			\
	text *txt = cstring_to_text(str);						\
	pfree(str);												\
	PG_RETURN_TEXT_P(txt);									\
}															\
DECLARE_TO_CHAR_FMT(type, to_char_fun)

DECLARE_NUM_TO_CHAR(int4, int4out, int4_to_char);
DECLARE_NUM_TO_CHAR(int8, int8out, int8_to_char);
DECLARE_NUM_TO_CHAR(float4, float4out, float4_to_char);
DECLARE_NUM_TO_CHAR(float8, float8out, float8_to_char);
DECLARE_NUM_TO_CHAR(numeric, numeric_out, numeric_to_char);

#define DECLARE_TIME_TO_CHAR(type, fmt_str, fmt_fun)		\
Datum ora_##type##_tochar(PG_FUNCTION_ARGS)					\
{															\
	Datum result;											\
	text *fmt;												\
	if (fmt_str && fmt_str[0] != '\0')						\
	{														\
		fmt = cstring_to_text(fmt_str);						\
		result = DirectFunctionCall2(fmt_fun,				\
									 PG_GETARG_DATUM(0),	\
									 PointerGetDatum(fmt));	\
		pfree(fmt);											\
		return result;										\
	}														\
	PG_RETURN_NULL();										\
}															\
DECLARE_TO_CHAR_FMT(type, fmt_fun)

DECLARE_TIME_TO_CHAR(timestamp, nls_timestamp_format, timestamp_to_char);
DECLARE_TIME_TO_CHAR(timestamp_tz, nls_timestamp_tz_format, timestamptz_to_char);
DECLARE_TIME_TO_CHAR(interval, nls_timestamp_format, interval_to_char);

#define BEGIN_TRUNC_TEXT(type, action_)								\
Datum trunc_text_to##type(PG_FUNCTION_ARGS)							\
{																	\
	text   *txt = PG_GETARG_TEXT_PP(0);								\
	char   *txtstr;													\
	Datum	result;													\
	int64	i64;													\
	txtstr = text_to_cstring(txt);									\
	if (scanint8(txtstr, true, &i64))								\
	{																\
		action_();													\
	}else															\
	{																\
		result = DirectFunctionCall3(numeric_in,					\
									 CStringGetDatum(txtstr),		\
									 ObjectIdGetDatum(InvalidOid),	\
									 Int32GetDatum(-1));			\
		result = DirectFunctionCall2(numeric_trunc,					\
									 result,						\
									 Int32GetDatum(0));				\
		result = DirectFunctionCall1(numeric_##type, result);		\
	}																\
	PG_RETURN_DATUM(result);										\
}extern int not_exists

/* trunc_text_toint2 */
#define TRUNC_INT2_ACTION()											\
	if (unlikely((int16)i64 != i64))								\
		ereport(ERROR,												\
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),		\
				 errmsg("value \"%s\" is out of range for type %s",	\
						txtstr, "smallint")));						\
	result = Int16GetDatum((int16)i64)
BEGIN_TRUNC_TEXT(int2, TRUNC_INT2_ACTION);

/* trunc_text_toint4 */
#define TRUNC_INT4_ACTION()											\
	if (unlikely((int32)i64 != i64))								\
		ereport(ERROR,												\
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),		\
				 errmsg("value \"%s\" is out of range for type %s",	\
						txtstr, "integer")));						\
	result = Int32GetDatum((int32)i64)
BEGIN_TRUNC_TEXT(int4, TRUNC_INT4_ACTION);

/* trunc_text_toint8 */
#define TRUNC_INT8_ACTION()	result = Int64GetDatum(i64)
BEGIN_TRUNC_TEXT(int8, TRUNC_INT8_ACTION);

Datum
text_toint2(PG_FUNCTION_ARGS)
{
	text   *txt = PG_GETARG_TEXT_PP_IF_NULL(0);
	char   *txtstr;
	int16	result;

	txtstr = text_to_cstring(txt);
	result = pg_strtoint16(txtstr);
	pfree(txtstr);

	PG_RETURN_INT16(result);
}

Datum
text_toint4(PG_FUNCTION_ARGS)
{
	text   *txt = PG_GETARG_TEXT_PP_IF_NULL(0);
	char   *txtstr;
	int32	result;

	txtstr = text_to_cstring(txt);
	result = pg_strtoint32(txtstr);
	pfree(txtstr);

	PG_RETURN_INT32(result);
}

Datum
text_toint8(PG_FUNCTION_ARGS)
{
	text   *txt = PG_GETARG_TEXT_PP_IF_NULL(0);
	char   *txtstr;
	int64	result;

	txtstr = text_to_cstring(txt);
	scanint8(txtstr, false, &result);
	pfree(txtstr);

	PG_RETURN_INT64(result);
}


Datum
text_tofloat4(PG_FUNCTION_ARGS)
{
	text   *txt = PG_GETARG_TEXT_PP_IF_NULL(0);
	char   *txtstr = NULL;
	Datum	result;

	txtstr = text_to_cstring(txt);
	result = DirectFunctionCall1(float4in,
								 CStringGetDatum(txtstr));
	pfree(txtstr);

	PG_RETURN_DATUM(result);
}

Datum
text_tofloat8(PG_FUNCTION_ARGS)
{
	text   *txt = PG_GETARG_TEXT_PP_IF_NULL(0);
	char   *txtstr = NULL;
	Datum	result;

	txtstr = text_to_cstring(txt);
	result = DirectFunctionCall1(float8in,
								 CStringGetDatum(txtstr));
	pfree(txtstr);

	PG_RETURN_DATUM(result);
}

#define DECLARE_TEXT_TO_TIME(type, tz, in_fun, typmod, is_to_date)	\
Datum ora_text_to_##type(PG_FUNCTION_ARGS)							\
{																	\
	text *txt = PG_GETARG_TEXT_P(0);								\
	char *str = text_to_cstring(txt);								\
	Datum result;													\
	result = DirectFunctionCall3(in_fun,							\
								 CStringGetDatum(str),				\
								 ObjectIdGetDatum(InvalidOid),		\
								 Int32GetDatum(typmod));			\
	pfree(str);														\
	PG_RETURN_DATUM(result);										\
}																	\
Datum ora_text_to_##type##_fmt(PG_FUNCTION_ARGS)					\
{																	\
	Pointer ptr = PG_GETARG_POINTER(1);								\
	if (likely(VARSIZE_ANY_EXHDR(ptr) > 0))							\
	{																\
		if (is_to_date)												\
		{															\
			TEXT_TO_TIME_CHECK_TEXT((text*)ptr);					\
			PG_RETURN_DATUM(ora_to_date(PG_GETARG_TEXT_P(0), (text*)ptr, tz));		\
		}															\
		else														\
		{															\
			TEXT_TO_TIME_CHECK_TEXT((text*)ptr);					\
			PG_RETURN_DATUM(ora_to_timestamp(PG_GETARG_TEXT_P(0), (text*)ptr, tz));	\
		}															\
	}																\
	PG_RETURN_NULL();												\
}extern int not_exists

/* ora_text_to_date, ora_text_to_date_fmt */
#define TEXT_TO_TIME_CHECK(fmt_str)											\
	do{																		\
		const char *p = fmt_str;											\
		while (*p != '\0')													\
		{																	\
			if (*p == 'f' || *p++ == 'F')									\
				ereport(ERROR,												\
						(errmsg("The date format is not recognized.")));	\
		}																	\
	}while(0)
#define TEXT_TO_TIME_CHECK_TEXT(txt)										\
	do{																		\
		char *str = text_to_cstring(txt);									\
		TEXT_TO_TIME_CHECK(str);											\
		pfree(str);															\
	}while(0)
DECLARE_TEXT_TO_TIME(date, false, timestamp_in, 0, true);

#undef TEXT_TO_TIME_CHECK_TEXT
#define TEXT_TO_TIME_CHECK_TEXT(txt) ((void)0)
/* ora_text_to_timestamp, ora_text_to_timestamp_fmt */
DECLARE_TEXT_TO_TIME(timestamp, false, timestamp_in, -1, false);
/* ora_text_to_timestamp_tz, ora_text_to_timestamp_tz_fmt */
DECLARE_TEXT_TO_TIME(timestamp_tz, true, timestamptz_in, 0, false);

Datum
text_tocstring(PG_FUNCTION_ARGS)
{
	text	*txt = PG_GETARG_TEXT_PP_IF_NULL(0);

	if (!txt)
		PG_RETURN_NULL();

	return CStringGetDatum(text_to_cstring(txt));
}

/*
 * oracle function to_number(text, text)
 * 		convert string to numeric
 */
Datum
text_tonumber(PG_FUNCTION_ARGS)
{
	text *txt = PG_GETARG_TEXT_P(0);
	char *txtstr;
	Datum result;

	if (likely(VARSIZE_ANY_EXHDR(txt) > 0))
	{
		txtstr = text_to_cstring(txt);
		result = DirectFunctionCall3(numeric_in,
									 CStringGetDatum(txtstr),
									 ObjectIdGetDatum(InvalidOid),
									 Int32GetDatum(-1));
		pfree(txtstr);

		PG_RETURN_DATUM(result);
	}
	PG_RETURN_NULL();
}

Datum text_tonumber_fmt(PG_FUNCTION_ARGS)
{
	Pointer ptr1 = PG_GETARG_POINTER(0);
	Pointer ptr2 = PG_GETARG_POINTER(1);
	if (likely(VARSIZE_ANY_EXHDR(ptr1) > 0 &&
			   VARSIZE_ANY_EXHDR(ptr2) > 0))
		PG_RETURN_DATUM(numeric_to_number(fcinfo));
	PG_RETURN_NULL();
}

Datum
float4_tonumber_fmt(PG_FUNCTION_ARGS)
{
	text *fmt = PG_GETARG_TEXT_P(1);
	text *txt;
	char *str;
	Datum result;

	if (likely(VARSIZE_ANY_EXHDR(fmt) > 0))
	{
		str = DatumGetCString(DirectFunctionCall1(float4out, PG_GETARG_DATUM(0)));
		txt = cstring_to_text(str);
		result = DirectFunctionCall2(numeric_to_number,
									 PointerGetDatum(txt),
									 PointerGetDatum(fmt));
		pfree(txt);
		pfree(str);
		PG_RETURN_DATUM(result);
	}
	PG_RETURN_NULL();
}

Datum
float8_tonumber_fmt(PG_FUNCTION_ARGS)
{
	text *fmt = PG_GETARG_TEXT_PP_IF_NULL(1);
	text *txt;
	char *str;
	Datum result;

	if (likely(VARSIZE_ANY_EXHDR(fmt) > 0))
	{
		str = DatumGetCString(DirectFunctionCall1(float8out, PG_GETARG_DATUM(0)));
		txt = cstring_to_text(str);
		result = DirectFunctionCall2(numeric_to_number,
									 PointerGetDatum(txt),
									 PointerGetDatum(fmt));
		pfree(txt);
		pfree(str);
		PG_RETURN_DATUM(result);
	}
	PG_RETURN_NULL();
}

/* 3 is enough, but it is defined as 4 in backend code. */
#ifndef MAX_CONVERSION_GROWTH
#define MAX_CONVERSION_GROWTH  4
#endif

/*
 * Convert a tilde (~) to ...
 *	1: a full width tilde. (same as JA16EUCTILDE in oracle)
 *	0: a full width overline. (same as JA16EUC in oracle)
 */
#define JA_TO_FULL_WIDTH_TILDE	1

static const char *
TO_MULTI_BYTE_UTF8[95] =
{
	"\343\200\200",
	"\357\274\201",
	"\342\200\235",
	"\357\274\203",
	"\357\274\204",
	"\357\274\205",
	"\357\274\206",
	"\342\200\231",
	"\357\274\210",
	"\357\274\211",
	"\357\274\212",
	"\357\274\213",
	"\357\274\214",
	"\357\274\215",
	"\357\274\216",
	"\357\274\217",
	"\357\274\220",
	"\357\274\221",
	"\357\274\222",
	"\357\274\223",
	"\357\274\224",
	"\357\274\225",
	"\357\274\226",
	"\357\274\227",
	"\357\274\230",
	"\357\274\231",
	"\357\274\232",
	"\357\274\233",
	"\357\274\234",
	"\357\274\235",
	"\357\274\236",
	"\357\274\237",
	"\357\274\240",
	"\357\274\241",
	"\357\274\242",
	"\357\274\243",
	"\357\274\244",
	"\357\274\245",
	"\357\274\246",
	"\357\274\247",
	"\357\274\250",
	"\357\274\251",
	"\357\274\252",
	"\357\274\253",
	"\357\274\254",
	"\357\274\255",
	"\357\274\256",
	"\357\274\257",
	"\357\274\260",
	"\357\274\261",
	"\357\274\262",
	"\357\274\263",
	"\357\274\264",
	"\357\274\265",
	"\357\274\266",
	"\357\274\267",
	"\357\274\270",
	"\357\274\271",
	"\357\274\272",
	"\357\274\273",
	"\357\277\245",
	"\357\274\275",
	"\357\274\276",
	"\357\274\277",
	"\342\200\230",
	"\357\275\201",
	"\357\275\202",
	"\357\275\203",
	"\357\275\204",
	"\357\275\205",
	"\357\275\206",
	"\357\275\207",
	"\357\275\210",
	"\357\275\211",
	"\357\275\212",
	"\357\275\213",
	"\357\275\214",
	"\357\275\215",
	"\357\275\216",
	"\357\275\217",
	"\357\275\220",
	"\357\275\221",
	"\357\275\222",
	"\357\275\223",
	"\357\275\224",
	"\357\275\225",
	"\357\275\226",
	"\357\275\227",
	"\357\275\230",
	"\357\275\231",
	"\357\275\232",
	"\357\275\233",
	"\357\275\234",
	"\357\275\235",
#if JA_TO_FULL_WIDTH_TILDE
	"\357\275\236"
#else
	"\357\277\243"
#endif
};

static const char *
TO_MULTI_BYTE_EUCJP[95] =
{
	"\241\241",
	"\241\252",
	"\241\311",
	"\241\364",
	"\241\360",
	"\241\363",
	"\241\365",
	"\241\307",
	"\241\312",
	"\241\313",
	"\241\366",
	"\241\334",
	"\241\244",
	"\241\335",
	"\241\245",
	"\241\277",
	"\243\260",
	"\243\261",
	"\243\262",
	"\243\263",
	"\243\264",
	"\243\265",
	"\243\266",
	"\243\267",
	"\243\270",
	"\243\271",
	"\241\247",
	"\241\250",
	"\241\343",
	"\241\341",
	"\241\344",
	"\241\251",
	"\241\367",
	"\243\301",
	"\243\302",
	"\243\303",
	"\243\304",
	"\243\305",
	"\243\306",
	"\243\307",
	"\243\310",
	"\243\311",
	"\243\312",
	"\243\313",
	"\243\314",
	"\243\315",
	"\243\316",
	"\243\317",
	"\243\320",
	"\243\321",
	"\243\322",
	"\243\323",
	"\243\324",
	"\243\325",
	"\243\326",
	"\243\327",
	"\243\330",
	"\243\331",
	"\243\332",
	"\241\316",
	"\241\357",
	"\241\317",
	"\241\260",
	"\241\262",
	"\241\306",
	"\243\341",
	"\243\342",
	"\243\343",
	"\243\344",
	"\243\345",
	"\243\346",
	"\243\347",
	"\243\350",
	"\243\351",
	"\243\352",
	"\243\353",
	"\243\354",
	"\243\355",
	"\243\356",
	"\243\357",
	"\243\360",
	"\243\361",
	"\243\362",
	"\243\363",
	"\243\364",
	"\243\365",
	"\243\366",
	"\243\367",
	"\243\370",
	"\243\371",
	"\243\372",
	"\241\320",
	"\241\303",
	"\241\321",
#if JA_TO_FULL_WIDTH_TILDE
	"\241\301"
#else
	"\241\261"
#endif
};

Datum
ora_to_multi_byte(PG_FUNCTION_ARGS)
{
	text	   *src;
	text	   *dst;
	const char *s;
	char	   *d;
	int			srclen;
	int			dstlen;
	int			i;
	const char **map;

	switch (GetDatabaseEncoding())
	{
		case PG_UTF8:
			map = TO_MULTI_BYTE_UTF8;
			break;
		case PG_EUC_JP:
#if PG_VERSION_NUM >= 80300
		case PG_EUC_JIS_2004:
#endif
			map = TO_MULTI_BYTE_EUCJP;
			break;
		/*
		 * TODO: Add converter for encodings.
		 */
		default:	/* no need to convert */
			PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	src = PG_GETARG_TEXT_PP(0);
	s = VARDATA_ANY(src);
	srclen = VARSIZE_ANY_EXHDR(src);
	dst = (text *) palloc(VARHDRSZ + srclen * MAX_CONVERSION_GROWTH);
	d = VARDATA(dst);

	for (i = 0; i < srclen; i++)
	{
		unsigned char	u = (unsigned char) s[i];
		if (0x20 <= u && u <= 0x7e)
		{
			const char *m = map[u - 0x20];
			while (*m)
			{
				*d++ = *m++;
			}
		}
		else
		{
			*d++ = s[i];
		}
	}

	dstlen = d - VARDATA(dst);
	SET_VARSIZE(dst, VARHDRSZ + dstlen);

	PG_RETURN_TEXT_P(dst);
}

static int
getindex(const char **map, char *mbchar, int mblen)
{
	int		i;

	for (i = 0; i < 95; i++)
	{
		if (!memcmp(map[i], mbchar, mblen))
			return i;
	}

	return -1;
}

Datum
ora_to_single_byte(PG_FUNCTION_ARGS)
{
	text	   *src;
	text	   *dst;
	char	   *s;
	char	   *d;
	int			srclen;
	int			dstlen;
	const char **map;

	switch (GetDatabaseEncoding())
	{
		case PG_UTF8:
			map = TO_MULTI_BYTE_UTF8;
			break;
		case PG_EUC_JP:
#if PG_VERSION_NUM >= 80300
		case PG_EUC_JIS_2004:
#endif
			map = TO_MULTI_BYTE_EUCJP;
			break;
		/*
		 * TODO: Add converter for encodings.
		 */
		default:	/* no need to convert */
			PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	src = PG_GETARG_TEXT_PP(0);
	s = VARDATA_ANY(src);
	srclen = VARSIZE_ANY_EXHDR(src);

	/* XXX - The output length should be <= input length */
	dst = (text *) palloc0(VARHDRSZ + srclen);
	d = VARDATA(dst);

	while (*s && (s - VARDATA_ANY(src) < srclen))
	{
		char   *u = s;
		int		clen;
		int		mapindex;

		clen = pg_mblen(u);
		s += clen;

		if (clen == 1)
			*d++ = *u;
		else if ((mapindex = getindex(map, u, clen)) >= 0)
		{
			const char m = 0x20 + mapindex;
			*d++ = m;
		}
		else
		{
			memcpy(d, u, clen);
			d += clen;
		}
	}

	dstlen = d - VARDATA(dst);
	SET_VARSIZE(dst, VARHDRSZ + dstlen);

	PG_RETURN_TEXT_P(dst);
}
