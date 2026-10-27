#include "../StdAfx.h"
#include "../NodeInfo/User.h"
#include "cTable.h"
#include "cExecuter.h"
#include "cQueryManager.h"


extern void Trace(const char* format, ...);



//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

cExecuter::cExecuter(void)
{
	Init();
}

cExecuter::~cExecuter(void)
{
	Destroy();
}

void cExecuter::Init()
{
	m_query.clear();
	m_factors.clear();
	m_serialize.Reset();
}

void cExecuter::Destroy()
{
	m_query.clear();
	m_factors.clear();
	m_serialize.Reset();
}


//////////////////////////////////////////////////////////////////////
// �ӽ��Լ� - ����׿�
//////////////////////////////////////////////////////////////////////

void cExecuter::Debug(const TCHAR *format, ...)
{
	TCHAR buffer[512];

	va_list marker; 
	va_start(marker, format); 
	vsprintf_s(buffer, format, marker); 
	va_end(marker);
	
	Trace("%s", buffer);
}

//////////////////////////////////////////////////////////////////////
// cExecuterADO::Operations
//////////////////////////////////////////////////////////////////////
cExecuterADO::cExecuterADO(void)
{
	if(!m_database.Startup())
	{
		Information( " >>>> Dismatch ADO component\n" );
	}
}
cExecuterADO::~cExecuterADO(void)
{	
	m_database.Cleanup();
}

// DB�� ����
BOOL cExecuterADO::OpenDatabase(const uint32 databaseId, const TCHAR* serverIP, const uint32 serverPort, const TCHAR* database, const TCHAR* userId, const TCHAR* userPassword)
{
	if(m_database.Create(databaseId))
	{
		if(m_database.Open(
			databaseId,
			userId,
			userPassword,
			database,
			serverIP,
			serverPort) )
		{
			return TRUE;
		}
	}
	return FALSE;
}

BOOL cExecuterADO::Execute(User* user, CQueryData *queryData, CQueryResultData& result)
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
			0 );
		return TRUE;
	}

	if(!queryData) 
	{
		Debug(_T("����(%lu) ���۰� ����(%lu)\r\n"), queryData->GetQueryID());
		return FALSE;
	}

	m_query.clear();
	m_factors.clear();

	int valueSize  = 0;
	int resultSize = 0;

	if(!g_queryManager.GetQuery(queryData->GetQueryID(), m_query))
	{
		Debug(_T(" : �������� �ʴ� ����(%lu)\r\n"), queryData->GetQueryID());
		LOG.PrintTimeAndLog(0, "QUERY NOT EXIST : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
		return FALSE;
	}

	if(!g_queryManager.GetFactors(queryData->GetQueryID(), m_factors))
	{
		Debug(_T(" : query.lua ���� ���� <input fields>\r\n"));
		LOG.PrintTimeAndLog(0, "QUERY ERROR : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
		return FALSE;
	}

	// ������Ŷ�� ���� ����
	if(queryData->GetResultType() != _RESULT_CHECK)
	{	
		if(!m_database.Execute(queryData->GetDatabaseID(), queryData, m_factors, m_query.c_str()))
		{
			Debug(_T(" : ��������(%s:%lu)\r\n"), user->GetIP(), user->GetPort() );
			if(queryData->GetResultType() == _RESULT_NAUGHT)
			{
				LOG.PrintTimeAndLog(0, "QUERY FAILED : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
			}
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
				LOG.PrintTimeAndLog(0, "QUERY OVERFLOW(extra) : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
				return FALSE;
			}
		}

		//Debug(_T(" : ���� ���� (%lu:%s)\r\n"), databaseId, m_query.c_str());

		cTable table;
		table.SetQueryID( queryData->GetQueryID() ); // �׽�Ʈ�뵵
		if(!m_database.Execute(queryData->GetDatabaseID(), queryData, m_factors, m_query.c_str(), &table))
		{	
			Debug(_T(" : ��������(%s:%lu)\r\n"), user->GetIP(), user->GetPort() );
			LOG.PrintTimeAndLog(0, "QUERY FAILED : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
			return FALSE;
		}
		else
		{
			vVALUETYPE resultTypes;
			queryData->GetResults(resultTypes);

			//if((resultTypes.size() == 0) && (0 == extraSize))
			//{
			//	// ������ �ʿ����
			//	return FALSE;
			//}

			if(resultTypes.size() != table.GetFieldCount())
			{
				Debug(_T(" : ��������(%s:%lu)\r\n"), user->GetIP(), user->GetPort() );
				LOG.PrintTimeAndLog(0, "QUERY DISMATCH FIELDS : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
				return FALSE;
			}

			//Debug(_T(" : ���� (%lu)\r\n"), packet.receiver);
			if(table.GetCount() > 0)
			{
				while(table.Assert())
				{
					for(uint16 index = 0 ; index < resultTypes.size() ; index++)
					{
						ValueType field = resultTypes[index];
						switch(field.type)
						{
						case vSHORT :
							{
								uint16 value;
								if(!table.Get(index, value)) return FALSE;

								if(!m_serialize.Write(value))
								{
									LOG.PrintTimeAndLog(0, "QUERY OVERFLOW : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
									return FALSE;
								}
							}
							break;
						case vLONG :
							{
								uint32 value;
								if(!table.Get(index, value)) return FALSE;

								if(!m_serialize.Write(value))
								{
									LOG.PrintTimeAndLog(0, "QUERY OVERFLOW : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
									return FALSE;
								}
							}
							break;
						case vINT64 :
							{
								uint64 value;
								if(!table.Get(index, value)) return FALSE;

								if(!m_serialize.Write(value))
								{
									LOG.PrintTimeAndLog(0, "QUERY OVERFLOW : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
									return FALSE;
								}
							}
							break;
						case vChar :
							{
								TCHAR temp[4096];
								if(!table.Get(index, temp, field.size)) return FALSE;	// size ���� �ؼ� ����� �غ���
							
								if(!m_serialize.Write(reinterpret_cast<uint8*>(temp), field.size, FALSE))
								{
									LOG.PrintTimeAndLog(0, "QUERY OVERFLOW : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
									return FALSE;
								}
							}
							break;
						case vWChar :
							{
								TCHAR temp[4096];
								if(!table.Get(index, temp, field.size)) return FALSE;	// size ���� �ؼ� ����� �غ���
							
								if(!m_serialize.Write(reinterpret_cast<uint8*>(temp), field.size, FALSE))
								{
									LOG.PrintTimeAndLog(0, "QUERY OVERFLOW : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
									return FALSE;
								}
							}
							break;
						case vTimeStamp :
							{
								kannaDbTimestamp value;
								if(!table.Get(index, value)) return FALSE;

								if(!m_serialize.Write((char*)&value, field.size))
								{
									LOG.PrintTimeAndLog(0, "QUERY OVERFLOW : (%s:%lu)%d", user->GetIP(), user->GetPort(), queryData->GetQueryID() );
									return FALSE;
								}
							}
							break;
						default :
							{
								return FALSE;
							}
						}
					}
					table.MoveNext();
				}
			}
			else
			{
				Debug("QUERY EMPTY : %d\n", queryData->GetQueryID() );
				// ��� ���ڵ尡 �������� �ʴ´�
				//LOG.PrintTimeAndLog(0, "QUERY EMPTY : %d", queryData->GetQueryID() );
			}
		}

		result.SetResultData( 
			queryData->GetIndex(), 
			queryData->GetMsgType(), 
			(int)(1), 
			reinterpret_cast<char*>(m_serialize.GetBuffer()),
			m_serialize.GetLength(),
			table.GetCount() );
	}
	return TRUE;
}
