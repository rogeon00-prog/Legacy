#pragma once

#pragma warning ( disable : 4995 )

#include "../iocpSocketDLL/iocpSocketDLL.h"
#include "../include/Log.h"
#include "Util/ioHashString.h"
#include "Protocol.h"
#include <stdio.h>
#include <windows.h>
#include <Direct.h>
#include <mmsystem.h>

#include "QueryData/QueryData.h"
#include "QueryData/QueryResultData.h"
#include "../Log/ioLogQueue.h"

extern CLog LOG;
extern CLog ReportLOG;
extern LONG WINAPI UnHandledExceptionFilter(struct _EXCEPTION_POINTERS *exceptionInfo);

#define COMPARE(x,min,max)		(((x)>=(min))&&((x)<(max)))
#define SAFEDELETE(x)			if(x != NULL) { delete x; x = NULL; }
#define SAFEDELETEARRAY(x)		if(x != NULL) { delete[] x; x = NULL; }

// DBTIMESTAMP/SQL_TIMESTAMP_STRUCT ada di sqltypes.h hanya jika ODBCVER >= 0x0300
// dan tidak tersedia di sebagian SDK -> sediakan tipe sendiri dgn layout identik
// (dipakai binding SQL_C_TYPE_TIMESTAMP). Guard agar tidak dobel definisi.
#ifndef KANNA_DB_TIMESTAMP_DEFINED
#define KANNA_DB_TIMESTAMP_DEFINED
typedef struct kannaDbTimestamp
{
	short			year;
	unsigned short	month;
	unsigned short	day;
	unsigned short	hour;
	unsigned short	minute;
	unsigned short	second;
	unsigned int	fraction;
} kannaDbTimestamp;
#endif
