// cDatabaseODBC.cpp - akses DB via ODBC (unixODBC + FreeTDS) untuk Wine.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cDatabaseODBC.h"
#include "../Util/cSerialize.h"

#pragma comment(lib, "odbc32.lib")

extern void Trace(const char* format, ...);
extern void Information(const char* format, ...);

#ifndef SQL_NTS
#define SQL_NTS (-3)
#endif

#ifndef SQL_NTSDATA
#define SQL_NTSDATA SQL_NTS
#endif

#ifndef SQL_NO_TOTAL
#define SQL_NO_TOTAL (-4)
#endif

// ---------------------------------------------------------------------------
// helper
// ---------------------------------------------------------------------------

static const char* OdbcDriverName()
{
	static char name[128] = {0};
	if(name[0] == 0)
	{
		const char* env = getenv("KANNA_ODBC_DRIVER");
		_snprintf_s(name, sizeof(name), _TRUNCATE, "%s", (env && *env) ? env : "FreeTDS32");
	}
	return name;
}

static const char* OdbcExtraOptions()
{
	static char opt[256] = {0};
	if(opt[0] == 0)
	{
		const char* env = getenv("KANNA_ODBC_OPTIONS");
		_snprintf_s(opt, sizeof(opt), _TRUNCATE, "%s",
			(env && *env) ? env : "TDS_Version=7.4;ClientCharset=UTF-8;");
	}
	return opt;
}

static void ParseTimestamp(const std::string& text, DBTIMESTAMP& out)
{
	ZeroMemory(&out, sizeof(out));
	if(text.empty())
		return;

	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, frac = 0;
	int n = sscanf_s(text.c_str(), "%d-%d-%d%*c%d:%d:%d.%d", &y, &mo, &d, &h, &mi, &s, &frac);
	if(n < 6)
	{
		n = sscanf_s(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
		frac = 0;
	}
	if(n < 3)
		return;

	out.year	= (SQLUSMALLINT)y;
	out.month	= (SQLUSMALLINT)mo;
	out.day		= (SQLUSMALLINT)d;
	out.hour	= (SQLUSMALLINT)h;
	out.minute	= (SQLUSMALLINT)mi;
	out.second	= (SQLUSMALLINT)s;
	out.fraction= (SQLUINTEGER)(frac * 1000000);	// pecahan teks = milidetik
}

// ---------------------------------------------------------------------------
// cOdbcTable
// ---------------------------------------------------------------------------

const cOdbcCell* cOdbcTable::Cell(const short field)
{
	if(!Assert())
		return NULL;

	const std::vector<cOdbcCell>& row = m_rows[m_cursor];
	if(field < 0 || field >= (short)row.size())
		return NULL;

	return &row[field];
}

bool cOdbcTable::Get(const short field, int8& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (int8)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, int16& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (int16)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, int32& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (int32)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, int64& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, uint8& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (uint8)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, uint16& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (uint16)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, uint32& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (uint32)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, uint64& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (uint64)(c->isNull ? 0 : c->integer);
	return true;
}

bool cOdbcTable::Get(const short field, float& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (float)(c->isNull ? 0.0 : c->real);
	return true;
}

bool cOdbcTable::Get(const short field, double& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = (c->isNull ? 0.0 : c->real);
	return true;
}

bool cOdbcTable::Get(const short field, DBTIMESTAMP& value)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	value = c->time;
	return true;
}

bool cOdbcTable::Get(const short field, char* value, const int size)
{
	const cOdbcCell* c = Cell(field);
	if(!c) return false;
	if(!value || size <= 0) return false;

	if(c->isNull)
	{
		ZeroMemory(value, size);
		return true;
	}

	// ADO: ukuran 1 = nilai tunggal (BYTE), bukan string
	if(size == 1)
	{
		value[0] = (char)(c->integer & 0xFF);
		return true;
	}

	ZeroMemory(value, size);
	uint32 length = (uint32)c->text.size();
	if(length > (uint32)(size - 1))
		length = (uint32)(size - 1);
	if(length > 0)
		memcpy(value, c->text.c_str(), length);
	return true;
}

// ---------------------------------------------------------------------------
// cOdbcConnection
// ---------------------------------------------------------------------------

cOdbcConnection::cOdbcConnection()
{
	Init();
}

cOdbcConnection::~cOdbcConnection()
{
	Destroy();
}

void cOdbcConnection::Init()
{
	m_env	= SQL_NULL_HENV;
	m_dbc	= SQL_NULL_HDBC;
	m_stmt	= SQL_NULL_HSTMT;
	m_connName.clear();
	m_paramBuffer.clear();
	m_paramLength.clear();
	m_paramIndicator.clear();
}

void cOdbcConnection::Destroy()
{
	ReleaseStatement();

	if(m_dbc != SQL_NULL_HDBC)
	{
		SQLDisconnect(m_dbc);
		SQLFreeHandle(SQL_HANDLE_DBC, m_dbc);
		m_dbc = SQL_NULL_HDBC;
	}
	if(m_env != SQL_NULL_HENV)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, m_env);
		m_env = SQL_NULL_HENV;
	}
	m_connName.clear();
}

void cOdbcConnection::ReleaseStatement()
{
	if(m_stmt != SQL_NULL_HSTMT)
	{
		SQLFreeStmt(m_stmt, SQL_CLOSE);
		SQLFreeHandle(SQL_HANDLE_STMT, m_stmt);
		m_stmt = SQL_NULL_HSTMT;
	}
}

void cOdbcConnection::SetText(SQLSMALLINT type, SQLHANDLE handle, std::string& out)
{
	SQLCHAR		state[6]	= {0};
	SQLINTEGER	native		= 0;
	SQLCHAR		text[512]	= {0};
	SQLSMALLINT	length		= 0;

	out.clear();
	if(SQLGetDiagRec(type, handle, 1, state, &native, text, sizeof(text) - 1, &length) == SQL_SUCCESS)
	{
		char buffer[640] = {0};
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "[%s/%ld] %s",
			(const char*)state, (long)native, (const char*)text);
		out = buffer;
	}
}

void cOdbcConnection::LogOdbcError(const char* tag, const char* queryName, SQLSMALLINT handleType, SQLHANDLE handle)
{
	std::string diag;
	SetText(handleType, handle, diag);

	Information("### ODBC %s (%s) : %s\n", tag, queryName ? queryName : "-", diag.c_str());
	Trace("ODBC %s (%s) : %s\n", tag, queryName ? queryName : "-", diag.c_str());
}

BOOL cOdbcConnection::Create()
{
	if(m_env == SQL_NULL_HENV)
	{
		if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env)))
		{
			Information(">>> ODBC env alloc failed\n");
			return FALSE;
		}
		SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	}

	if(m_dbc == SQL_NULL_HDBC)
	{
		if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_dbc)))
		{
			Information(">>> ODBC dbc alloc failed\n");
			return FALSE;
		}
		SQLSetConnectAttr(m_dbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)15, 0);
	}
	return TRUE;
}

BOOL cOdbcConnection::Open(const char* userName, const char* password, const char* connName)
{
	if(!Create())
		return FALSE;

	m_connName = (connName && *connName) ? connName : "";

	if(m_connName.empty())
		return FALSE;

	SQLCHAR		out[2048]	= {0};
	SQLSMALLINT	outLen		= 0;

	SQLRETURN	ret = SQLDriverConnectA(
		m_dbc,
		NULL,
		(SQLCHAR*)m_connName.c_str(),
		(SQLSMALLINT)m_connName.size(),
		out,
		sizeof(out),
		&outLen,
		SQL_DRIVER_NOPROMPT);

	if(!SQL_SUCCEEDED(ret))
	{
		LogOdbcError("connect", m_connName.c_str(), SQL_HANDLE_DBC, m_dbc);
		Close();
		return FALSE;
	}

	return TRUE;
}

BOOL cOdbcConnection::Open(const char* userName, const char* password, const char* dbName, const char* IP, const uint32 port)
{
	char conn[1024] = {0};

	// DSN-less connection string (unixODBC resolve DRIVER dari /etc/odbcinst.ini)
	_snprintf_s(conn, sizeof(conn), _TRUNCATE,
		"DRIVER={%s};Server=%s;Port=%lu;Database=%s;UID=%s;PWD=%s;%s",
		OdbcDriverName(),
		(IP && *IP) ? IP : "127.0.0.1",
		(unsigned long)(port ? port : 1433),
		(dbName && *dbName) ? dbName : "",
		(userName && *userName) ? userName : "",
		(password && *password) ? password : "",
		OdbcExtraOptions());

	return Open(userName, password, conn);
}

void cOdbcConnection::Close()
{
	ReleaseStatement();

	if(m_dbc != SQL_NULL_HDBC)
	{
		SQLDisconnect(m_dbc);
		SQLFreeHandle(SQL_HANDLE_DBC, m_dbc);
		m_dbc = SQL_NULL_HDBC;
	}
}

BOOL cOdbcConnection::Prepare(CQueryData* queryData, FACTORS& factorList, const char* queryName)
{
	if(!queryData)
	{
		Information("query data kosong\n");
		return FALSE;
	}
	if(m_dbc == SQL_NULL_HDBC)
	{
		Information("### db connection kosong\n");
		return FALSE;
	}

	ReleaseStatement();

	if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, m_dbc, &m_stmt)))
	{
		LogOdbcError("alloc stmt", queryName, SQL_HANDLE_DBC, m_dbc);
		return FALSE;
	}

	m_paramBuffer.clear();
	m_paramLength.clear();
	m_paramIndicator.clear();

	cSerialize fields;
	queryData->GetFields(fields);

	// ---- pass 1: kumpulkan nilai parameter (harus hidup sampai SQLExecute) ----
	struct ParamSlot { short cType; short sqlType; SQLULEN columnSize; SQLLEN* indicator; void* data; };
	std::vector<ParamSlot> slots;

	for(FACTORS::const_iterator it = factorList.begin(); it != factorList.end(); ++it)
	{
		ParamSlot slot;
		slot.indicator = NULL;
		slot.data = NULL;

		switch(*it)
		{
		case FT_INT8:
			{
				uint8 value = 0;
				if(!fields.GetUInt(value)) return FALSE;

				std::vector<char> buf(sizeof(int16));
				int16 v = (int16)value;
				memcpy(&buf[0], &v, sizeof(v));
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(0);

				slot.cType = SQL_C_SSHORT;
				slot.sqlType = SQL_SMALLINT;
				slot.columnSize = 5;
			}
			break;
		case FT_INT16:
			{
				int16 value = 0;
				if(!fields.GetInt(value)) return FALSE;

				std::vector<char> buf(sizeof(int16));
				memcpy(&buf[0], &value, sizeof(value));
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(0);

				slot.cType = SQL_C_SSHORT;
				slot.sqlType = SQL_SMALLINT;
				slot.columnSize = 5;
			}
			break;
		case FT_INT32:
			{
				int32 value = 0;
				if(!fields.GetInt(value)) return FALSE;

				std::vector<char> buf(sizeof(int32));
				memcpy(&buf[0], &value, sizeof(value));
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(0);

				slot.cType = SQL_C_SLONG;
				slot.sqlType = SQL_INTEGER;
				slot.columnSize = 10;
			}
			break;
		case FT_INT64:
			{
				int64 value = 0;
				if(!fields.GetInt(value)) return FALSE;

				std::vector<char> buf(sizeof(int64));
				memcpy(&buf[0], &value, sizeof(value));
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(0);

				slot.cType = SQL_C_SBIGINT;
				slot.sqlType = SQL_BIGINT;
				slot.columnSize = 19;
			}
			break;
		case FT_FLOAT:
			{
				float value = 0;
				if(!fields.GetFloat(value)) return FALSE;

				std::vector<char> buf(sizeof(double));
				double v = (double)value;
				memcpy(&buf[0], &v, sizeof(v));
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(0);

				slot.cType = SQL_C_DOUBLE;
				slot.sqlType = SQL_DOUBLE;
				slot.columnSize = 15;
			}
			break;
		case FT_DATETIME:
			{
				SYSTEMTIME value;
				ZeroMemory(&value, sizeof(value));
				if(!fields.GetBuffer((uint8*)&value, sizeof(SYSTEMTIME))) return FALSE;

				DBTIMESTAMP ts;
				ZeroMemory(&ts, sizeof(ts));
				ts.year		= (SQLUSMALLINT)value.wYear;
				ts.month	= (SQLUSMALLINT)value.wMonth;
				ts.day		= (SQLUSMALLINT)value.wDay;
				ts.hour		= (SQLUSMALLINT)value.wHour;
				ts.minute	= (SQLUSMALLINT)value.wMinute;
				ts.second	= (SQLUSMALLINT)value.wSecond;
				ts.fraction	= (SQLUINTEGER)value.wMilliseconds * 1000000;

				std::vector<char> buf(sizeof(ts));
				memcpy(&buf[0], &ts, sizeof(ts));
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(sizeof(ts));

				slot.cType = SQL_C_TYPE_TIMESTAMP;
				slot.sqlType = SQL_TIMESTAMP;
				slot.columnSize = 23;
			}
			break;
		case FT_STRING:
			{
				char temp[4096] = {0};
				if(!fields.GetString(temp, sizeof(temp))) return FALSE;

				uint32 length = (uint32)_tcslen(temp);
				if(!length) length = 1;

				std::vector<char> buf(length + 1, 0);
				_snprintf_s(&buf[0], buf.size(), _TRUNCATE, "%s", temp);
				m_paramBuffer.push_back(buf);
				m_paramIndicator.push_back(length);

				slot.cType = SQL_C_CHAR;
				slot.sqlType = SQL_VARCHAR;
				slot.columnSize = length;
			}
			break;
		default:
			return FALSE;
		}

		slots.push_back(slot);
	}

	// ---- susun perintah: EXEC <sp> ?, ?, ... ----
	std::string sql("EXEC ");
	sql += queryName;
	if(!slots.empty())
	{
		sql += " ";
		for(size_t i = 0; i < slots.size(); i++)
			sql += (i == 0) ? "?" : ", ?";
	}

	if(!SQL_SUCCEEDED(SQLPrepareA(m_stmt, (SQLCHAR*)sql.c_str(), (SQLINTEGER)sql.size())))
	{
		LogOdbcError("prepare", queryName, SQL_HANDLE_STMT, m_stmt);
		return FALSE;
	}

	// ---- pass 2: bind parameter (buffer sudah stabil: vector sudah final) ----
	for(size_t i = 0; i < slots.size(); i++)
	{
		SQLRETURN ret = SQLBindParameter(
			m_stmt,
			(SQLUSMALLINT)(i + 1),
			SQL_PARAM_INPUT,
			slots[i].cType,
			slots[i].sqlType,
			slots[i].columnSize,
			0,
			m_paramBuffer[i].empty() ? (SQLPOINTER)"" : (SQLPOINTER)&m_paramBuffer[i][0],
			(SQLINTEGER)m_paramBuffer[i].size(),
			&m_paramIndicator[i]);

		if(!SQL_SUCCEEDED(ret))
		{
			LogOdbcError("bind", queryName, SQL_HANDLE_STMT, m_stmt);
			return FALSE;
		}
	}
	return TRUE;
}

BOOL cOdbcConnection::FetchResult(cOdbcTable* table)
{
	SQLSMALLINT columns = 0;
	if(!SQL_SUCCEEDED(SQLNumResultCols(m_stmt, &columns)))
	{
		LogOdbcError("numcols", "-", SQL_HANDLE_STMT, m_stmt);
		return FALSE;
	}

	if(!table)
	{
		// habiskan semua resultset (rowcount dsb) supaya koneksi siap dipakai lagi
		while(SQLMoreResults(m_stmt) == SQL_SUCCESS)
			;
		return TRUE;
	}

	table->Clear();
	table->SetFieldCount(columns);

	std::vector<char>	buffer(8192);
	SQLRETURN			fetchRet = SQL_SUCCESS;

	while(SQL_SUCCEEDED(fetchRet = SQLFetch(m_stmt)))
	{
		std::vector<cOdbcCell>& row = table->AddRow();
		row.resize(columns);

		for(SQLSMALLINT col = 1; col <= columns; col++)
		{
			cOdbcCell& cell = row[col - 1];

			std::string text;
			SQLLEN		indicator = 0;

			for(;;)
			{
				SQLRETURN getRet = SQLGetData(m_stmt, col, SQL_C_CHAR, &buffer[0], (SQLLEN)buffer.size(), &indicator);
				if(getRet == SQL_NO_DATA)
					break;
				if(!SQL_SUCCEEDED(getRet))
				{
					LogOdbcError("getdata", "-", SQL_HANDLE_STMT, m_stmt);
					break;
				}
				if(indicator == SQL_NULL_DATA)
				{
					cell.isNull = true;
					break;
				}

				size_t length = 0;
				if(indicator == SQL_NTSDATA || indicator == SQL_NO_TOTAL)
					length = strlen(&buffer[0]);
				else
					length = (size_t)indicator;

				if(length > 0)
					text.append(&buffer[0], length);

				if(getRet != SQL_SUCCESS_WITH_INFO)
					break;
			}

			if(!cell.isNull)
			{
				cell.text		= text;
				cell.integer	= (int64)_strtoi64(text.c_str(), NULL, 10);
				cell.real		= (double)strtod(text.c_str(), NULL);
				ParseTimestamp(text, cell.time);
			}
		}
	}

	if(fetchRet != SQL_NO_DATA && !SQL_SUCCEEDED(fetchRet))
	{
		// habiskan resultset walau gagal
		while(SQLMoreResults(m_stmt) == SQL_SUCCESS)
			;
		return FALSE;
	}

	while(SQLMoreResults(m_stmt) == SQL_SUCCESS)
		;

	return TRUE;
}

BOOL cOdbcConnection::Execute(CQueryData* queryData, FACTORS& factorList, const char* queryName, cOdbcTable* table)
{
	if(!Prepare(queryData, factorList, queryName))
		return FALSE;

	if(!SQL_SUCCEEDED(SQLExecute(m_stmt)))
	{
		LogOdbcError("execute", queryName, SQL_HANDLE_STMT, m_stmt);
		ReleaseStatement();

		// coba sambung ulang sekali (mengikuti perilaku ADO)
		if(Open("", "", m_connName.c_str()))
			return FALSE;

		return FALSE;
	}

	BOOL ok = FetchResult(table);
	return ok;
}

BOOL cOdbcConnection::Generate(CQueryData* queryData, FACTORS& factorList, const char* queryName, char* buffer, const int length)
{
	if(!queryData)
	{
		Information("query data kosong\n");
		return FALSE;
	}

	char column[1024] = {0};
	_snprintf_s(buffer, length, _TRUNCATE, "%s ", queryName);

	cSerialize fields;
	queryData->GetFields(fields);

	for(FACTORS::const_iterator it = factorList.begin(); it != factorList.end(); ++it)
	{
		switch(*it)
		{
		case FT_INT8:
			{
				uint8 value = 0;
				if(!fields.GetUInt(value)) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "%lu", (unsigned long)value);
			}
			break;
		case FT_INT16:
			{
				int16 value = 0;
				if(!fields.GetInt(value)) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "%d", (int)value);
			}
			break;
		case FT_INT32:
			{
				int32 value = 0;
				if(!fields.GetInt(value)) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "%d", (int)value);
			}
			break;
		case FT_INT64:
			{
				int64 value = 0;
				if(!fields.GetInt(value)) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "%I64d", value);
			}
			break;
		case FT_FLOAT:
			{
				float value = 0;
				if(!fields.GetFloat(value)) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "%lf", (double)value);
			}
			break;
		case FT_DATETIME:
			{
				SYSTEMTIME value;
				ZeroMemory(&value, sizeof(value));
				if(!fields.GetBuffer((uint8*)&value, sizeof(SYSTEMTIME))) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "'%d-%d-%d %d:%d:%d'",
					value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute, value.wSecond);
			}
			break;
		case FT_STRING:
			{
				char temp[4096] = {0};
				if(!fields.GetString(temp, sizeof(temp))) return FALSE;
				_snprintf_s(column, sizeof(column), _TRUNCATE, "'%s'", temp);
			}
			break;
		default:
			return FALSE;
		}

		if(it != factorList.begin())
			strcat_s(buffer, length, ", ");
		strcat_s(buffer, length, column);
	}
	return TRUE;
}

// ---------------------------------------------------------------------------
// cDatabaseODBC
// ---------------------------------------------------------------------------

cDatabaseODBC::cDatabaseODBC()
{
	Init();
}

cDatabaseODBC::~cDatabaseODBC()
{
	Destroy();
}

void cDatabaseODBC::Init()
{
	m_connectionTable.clear();
}

void cDatabaseODBC::Destroy()
{
	for(CONNECTION_TABLE::iterator it = m_connectionTable.begin(); it != m_connectionTable.end(); ++it)
	{
		if(it->second)
			delete it->second;
	}
	m_connectionTable.clear();
}

BOOL cDatabaseODBC::Startup()
{
	return TRUE;
}

void cDatabaseODBC::Cleanup()
{
	Destroy();
}

BOOL cDatabaseODBC::Create(const uint32 nID)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
	{
		cOdbcConnection* pConnection = new cOdbcConnection;
		m_connectionTable[nID] = pConnection;
		return TRUE;
	}
	return TRUE;
}

void cDatabaseODBC::Close(const uint32 nID)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
		return;

	if(it->second)
	{
		it->second->Close();
		delete it->second;
	}
	m_connectionTable.erase(it);
}

BOOL cDatabaseODBC::Open(const uint32 nID)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
		return FALSE;

	return it->second->Open("", "", "");
}

BOOL cDatabaseODBC::Open(const uint32 nID, const char* userName, const char* password, const char* connName)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
		return FALSE;

	return it->second->Open(userName, password, connName);
}

BOOL cDatabaseODBC::Open(const uint32 nID, const char* userName, const char* password, const char* dbName, const char* IP, const uint32 port)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
		return FALSE;

	return it->second->Open(userName, password, dbName, IP, port);
}

BOOL cDatabaseODBC::Execute(const uint32 nID, CQueryData* queryData, FACTORS& factorList, const char* queryName, cOdbcTable* table)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
		return FALSE;

	return it->second->Execute(queryData, factorList, queryName, table);
}

BOOL cDatabaseODBC::Generate(const uint32 nID, CQueryData* queryData, FACTORS& factorList, const char* queryName, char* buffer, const int length)
{
	CONNECTION_TABLE::iterator it = m_connectionTable.find(nID);
	if(it == m_connectionTable.end())
		return FALSE;

	return it->second->Generate(queryData, factorList, queryName, buffer, length);
}
