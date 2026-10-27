#pragma once

// ---------------------------------------------------------------------------
// cDatabaseODBC - implementasi akses DB via ODBC (unixODBC/FreeTDS di Wine).
// Dipakai sebagai pengganti cDatabaseADO supaya ls_dbagent bisa jalan di Wine
// tanpa komponen MDAC/ADO (yang broken: CLSID {6C736DB1} MSDAINITIALIZE).
// API-nya dibuat sama dengan cDatabaseADO supaya cExecuter bisa ganti backend
// hanya dengan mengubah tipe member.
// ---------------------------------------------------------------------------

#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <map>
#include <string>
#include <vector>

#include "../include/common.h"
#include "../QueryData/QueryData.h"
#include "Database.h"

// satu sel hasil (disimpan sebagai teks + hasil konversi)
struct cOdbcCell
{
	bool					isNull;
	int64					integer;
	double					real;
	DBTIMESTAMP				time;
	std::string				text;

	cOdbcCell() : isNull(true), integer(0), real(0)
	{
		ZeroMemory(&time, sizeof(time));
	}
};

class cOdbcTable
{
public:
	cOdbcTable() : m_cursor(0), m_fieldCount(0), m_queryId(0) {}
	virtual ~cOdbcTable() {}

	void	Clear()					{ m_rows.clear(); m_cursor = 0; m_fieldCount = 0; }
	void	SetFieldCount(int n)	{ m_fieldCount = n; }
	std::vector<cOdbcCell>&	AddRow() { m_rows.push_back(std::vector<cOdbcCell>()); return m_rows.back(); }

	int		GetFieldCount()	{ return m_fieldCount; }
	int		GetCount()		{ return (int)m_rows.size(); }
	bool	Assert()		{ return m_cursor < (int)m_rows.size(); }
	HRESULT	MoveNext()		{ if(!Assert()) return S_FALSE; ++m_cursor; return S_OK; }
	HRESULT	MovePrevious()	{ if(m_cursor > 0) --m_cursor; return S_OK; }
	HRESULT	MoveFirst()		{ m_cursor = 0; return S_OK; }
	HRESULT	MoveLast()		{ m_cursor = (int)m_rows.size(); return S_OK; }

	bool	Get(const short field, int8&  value);
	bool	Get(const short field, int16& value);
	bool	Get(const short field, int32& value);
	bool	Get(const short field, int64& value);

	bool	Get(const short field, uint8&  value);
	bool	Get(const short field, uint16& value);
	bool	Get(const short field, uint32& value);
	bool	Get(const short field, uint64& value);

	bool	Get(const short field, float&  value);
	bool	Get(const short field, double& value);
	bool	Get(const short field, DBTIMESTAMP& value);
	bool	Get(const short field, char* value, const int size);

	void	SetQueryID(const uint32 queryId) { m_queryId = queryId; }
	uint32	m_queryId;

private:
	const cOdbcCell* Cell(const short field);

	std::vector<std::vector<cOdbcCell> >	m_rows;
	int										m_cursor;
	int										m_fieldCount;
};

class cOdbcConnection
{
public:
	cOdbcConnection();
	virtual ~cOdbcConnection();

	void	Init();
	void	Destroy();

public:
	BOOL	Create();

	BOOL	Open(const char* userName, const char* password, const char* connName);
	BOOL	Open(const char* userName, const char* password, const char* dbName, const char* IP, const uint32 port);
	void	Close();

	BOOL	Execute(CQueryData* queryData, FACTORS& factorList, const char* queryName, cOdbcTable* table = NULL);
	BOOL	Generate(CQueryData* queryData, FACTORS& factorList, const char* queryName, char* buffer, const int length);

private:
	BOOL	Prepare(CQueryData* queryData, FACTORS& factorList, const char* queryName);
	BOOL	FetchResult(cOdbcTable* table);
	void	ReleaseStatement();
	void	LogOdbcError(const char* tag, const char* queryName, SQLSMALLINT handleType, SQLHANDLE handle);
	void	SetText(SQLSMALLINT type, SQLHANDLE handle, std::string& out);

private:
	SQLHENV		m_env;
	SQLHDBC		m_dbc;
	SQLHSTMT	m_stmt;

	std::string						m_connName;
	std::vector<std::vector<char> >	m_paramBuffer;	// nilai parameter (stabil sampai SQLExecute)
	std::vector<SQLLEN>				m_paramLength;
	std::vector<SQLLEN>				m_paramIndicator;
};

class cDatabaseODBC
{
public:
	cDatabaseODBC();
	virtual ~cDatabaseODBC();

	void	Init();
	void	Destroy();

public:
	BOOL	Startup();
	void	Cleanup();

	BOOL	Create(const uint32 nID);
	void	Close(const uint32 nID);

	BOOL	Open(const uint32 nID);
	BOOL	Open(const uint32 nID, const char* userName, const char* password, const char* connName);
	BOOL	Open(const uint32 nID, const char* userName, const char* password, const char* dbName, const char* IP, const uint32 port);

	BOOL	Execute(const uint32 nID, CQueryData* queryData, FACTORS& factorList, const char* queryName, cOdbcTable* table = NULL);
	BOOL	Generate(const uint32 nID, CQueryData* queryData, FACTORS& factorList, const char* queryName, char* buffer, const int length);

private:
	typedef std::map<uint32, cOdbcConnection*> CONNECTION_TABLE;

	CONNECTION_TABLE	m_connectionTable;
};
