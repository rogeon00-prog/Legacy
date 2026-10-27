// cExecuterODBC.cpp - versi ODBC dari cExecuterADO (untuk Wine + unixODBC/FreeTDS).
// Logika hasil (serialisasi) sengaja identik dengan cExecuterADO supaya protokol
// balasan ke gamesvr/mainsvr tidak berubah.
#include "../stdafx.h"
#include <windows.h>

#include "cExecuter.h"
#include "../NodeInfo/User.h"
#include "cTable.h"
#include "cQueryManager.h"

extern void Trace(const char* format, ...);
extern void Information(const char* format, ...);

extern cQueryManager g_queryManager;
extern void Debug(const char* format, ...);

//////////////////////////////////////////////////////////////////////
// cExecuterODBC
//////////////////////////////////////////////////////////////////////

cExecuterODBC::cExecuterODBC(void)
{
	if(!m_database.Startup())
	{
		Information(" >>>> ODBC component tidak siap\n");
	}
}

cExecuterODBC::~cExecuterODBC(void)
{
	m_database.Cleanup();
}

BOOL cExecuterODBC::OpenDatabase(const uint32 databaseId, const TCHAR* serverIP, const uint32 serverPort, const TCHAR* database, const TCHAR* userId, const TCHAR* userPassword)
{
	if(m_database.Create(databaseId))
	{
		if(m_database.Open(databaseId, userId, userPassword, database, serverIP, serverPort))
		{
			return TRUE;
		}
	}
	return FALSE;
}

BOOL cExecuterODBC::Execute(User* user, CQueryData* queryData, CQueryResultData& result)
{
	if(queryData->GetQueryID() == 1) // ping-pong
	{
		int extraSize;
		char extraBuffer[1024];
		queryData->GetReturns(extraBuffer, extraSize, 1024);

		m_serialize.Reset();
		if(extraSize > 0)
		{
			m_serialize.Write((uint8*)extraBuffer, extraSize);
		}
		result.SetResultData(
			queryData->GetIndex(),
			queryData->GetMsgType(),
			(int)(1),
			reinterpret_cast<char*>(m_serialize.GetBuffer()),
			m_serialize.GetLength(),
			0);
		return TRUE;
	}

	if(!queryData)
	{
		return FALSE;
	}

	m_query.clear();
	m_factors.clear();

	if(!g_queryManager.GetQuery(queryData->GetQueryID(), m_query))
	{
		Information("QUERY NOT EXIST : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
		return FALSE;
	}

	if(!g_queryManager.GetFactors(queryData->GetQueryID(), m_factors))
	{
		Information("QUERY ERROR : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
		return FALSE;
	}

	// result dikirim balik
	if(queryData->GetResultType() != _RESULT_CHECK)
	{
		if(!m_database.Execute(queryData->GetDatabaseID(), queryData, m_factors, m_query.c_str()))
		{
			Information("QUERY FAILED : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
		}
		return FALSE;
	}
	else
	{
		int extraSize;
		char extraBuffer[1024];
		queryData->GetReturns(extraBuffer, extraSize, 1024);

		m_serialize.Reset();
		if(extraSize > 0)
		{
			if(!m_serialize.Write((uint8*)extraBuffer, extraSize))
			{
				Information("QUERY OVERFLOW(extra) : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
				return FALSE;
			}
		}

		cOdbcTable table;
		table.SetQueryID(queryData->GetQueryID());

		if(!m_database.Execute(queryData->GetDatabaseID(), queryData, m_factors, m_query.c_str(), &table))
		{
			Information("QUERY FAILED : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
			return FALSE;
		}
		else
		{
			vVALUETYPE resultTypes;
			queryData->GetResults(resultTypes);

			if(resultTypes.size() != (size_t)table.GetFieldCount())
			{
				Information("QUERY DISMATCH FIELDS : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
				return FALSE;
			}

			if(table.GetCount() > 0)
			{
				while(table.Assert())
				{
					for(uint16 index = 0; index < resultTypes.size(); index++)
					{
						ValueType field = resultTypes[index];
						switch(field.type)
						{
						case vSHORT:
							{
								uint16 value = 0;
								if(!table.Get((short)index, value)) return FALSE;

								if(!m_serialize.Write(value))
								{
									Information("QUERY OVERFLOW : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
									return FALSE;
								}
							}
							break;
						case vLONG:
							{
								uint32 value = 0;
								if(!table.Get((short)index, value)) return FALSE;

								if(!m_serialize.Write(value))
								{
									Information("QUERY OVERFLOW : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
									return FALSE;
								}
							}
							break;
						case vINT64:
							{
								uint64 value = 0;
								if(!table.Get((short)index, value)) return FALSE;

								if(!m_serialize.Write(value))
								{
									Information("QUERY OVERFLOW : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
									return FALSE;
								}
							}
							break;
						case vChar:
						case vWChar:
							{
								TCHAR temp[4096];
								ZeroMemory(temp, sizeof(temp));
								if(!table.Get((short)index, temp, field.size)) return FALSE;

								if(!m_serialize.Write(reinterpret_cast<uint8*>(temp), field.size, FALSE))
								{
									Information("QUERY OVERFLOW : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
									return FALSE;
								}
							}
							break;
						case vTimeStamp:
							{
								DBTIMESTAMP value;
								ZeroMemory(&value, sizeof(value));
								if(!table.Get((short)index, value)) return FALSE;

								if(!m_serialize.Write((char*)&value, field.size))
								{
									Information("QUERY OVERFLOW : (%s:%lu)%d\n", user->GetIP(), user->GetPort(), queryData->GetQueryID());
									return FALSE;
								}
							}
							break;
						default:
							return FALSE;
						}
					}
					table.MoveNext();
				}
			}

			result.SetResultData(
				queryData->GetIndex(),
				queryData->GetMsgType(),
				(int)(1),
				reinterpret_cast<char*>(m_serialize.GetBuffer()),
				m_serialize.GetLength(),
				table.GetCount());
		}
	}
	return TRUE;
}
