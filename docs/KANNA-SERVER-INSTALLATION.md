# KANNA (Legacy Lost Saga) - Server Installation & Operation Guide

Dokumen panduan lengkap instalasi, arsitektur, kompilasi, dan pengoperasian server Kanna (Lost Saga Legacy) pada Linux VPS (`172.232.251.28`) mengikuti spesifikasi resmi upstream `https://github.com/noverapj/Legacy/tree/main/docs`.

---

## 1. Arsitektur Sistem

- **Upstream Source**: `https://github.com/noverapj/Legacy`
- **Active Fork & CI**: `https://github.com/necoffe/Legacy` (Branch: `main`)
- **Host Target**: Linux Debian 12 (x86_64) di `172.232.251.28`
- **Subdomain Portal/Patch**: `https://kanna.web.id` / `https://dl.kanna.web.id`
- **Runtime Environment**:
  - **Database Engine**: Microsoft SQL Server 2017 (Docker container `mssql`)
  - **Windows Compatibility Layer**: Wine 9.x (`WINEPREFIX=/root/.wine-novera`)
  - **Web & Patch Distribution**: Python 3 backend (`/root/register-server.py`) port 80/443

---

## 2. Struktur Direktori Server (`/root/kanna-server/`)

Sesuai spesifikasi `docs/CONFIG.md`, seluruh executable dan file konfigurasi runtime diorganisir sebagai berikut:

```
/root/kanna-server/
├── config.ini                   # Konfigurasi urutan start service & watchdog
├── global_define.ini            # Pemetaan IP seluruh service (172.232.251.28)
├── LSController.exe             # Server supervisor / watchdog controller
│
├── ls_dbagent/                  # Database Agent (Game & Log)
│   ├── ls_dbagent.exe
│   ├── ls_dbagent_game.ini      # Port 30101, SQL conn ke LosaGame
│   ├── ls_dbagent_log.ini       # Port 30102, SQL conn ke LosaGame_Log
│   └── ls_query.ini             # Stored procedure mapper
│
├── ls_billingsvr/               # Billing Server
│   ├── ls_billingsvr.exe
│   └── ls_billingsvr.ini        # Port 14011
│
├── ls_mainsvr/                  # Main Server
│   ├── ls_mainsvr.exe
│   ├── ls_auth.ini              # Auth configuration
│   └── config/                  # Server-wide game balance & rules
│
├── ls_loginsvr/                 # Login Server
│   ├── ls_loginsvr.exe
│   └── ls_loginsvr.ini          # Port 40005
│
├── ls_gamesvr/                  # Game Zone Server
│   ├── ls_gamesvr.exe
│   ├── ls_gamesvr.ini           # Port 20101 (Zone 1), 20102 (Zone 2)
│   └── config/tables/           # Mode tables (*.dat)
│
├── ls_relaysvr/                 # Relay Server (UDP packet proxy)
│   └── ls_relaysvr.exe
│
└── ls_filewritesvr/             # File Write Server (Asynchronous file writer)
    └── ls_filewritesvr.exe
```

---

## 3. Database Setup (MSSQL)

Sesuai `docs/DATABASE.md`, backend menggunakan Microsoft SQL Server 2017.

### 3.1 Status & Akses Docker
- Container: `mssql` (`mcr.microsoft.com/mssql/server:2017-latest`)
- Port: `127.0.0.1:1433` (hanya terekspos lokal)
- User: `sa`
- Password SA: `[REDACTED]`

### 3.2 Database yang Dibutuhkan
Urutan pembuatan/restore database:
1. `LosaLogData` (Script: `sql/linux/LosaLogData_UNICODE.sql`)
2. `LosaGame_Log` (Script: `sql/linux/LosaGame_Log_UNICODE.sql`)
3. `LosaGame` (Script: `sql/linux/LosaGame_UNICODE.sql`)

Command import database via `sqlcmd`:
```bash
docker exec -i mssql /opt/mssql-tools/bin/sqlcmd -S localhost -U sa -P '[REDACTED]' < /root/novera-legacy/sql/linux/LosaLogData_UNICODE.sql
docker exec -i mssql /opt/mssql-tools/bin/sqlcmd -S localhost -U sa -P '[REDACTED]' < /root/novera-legacy/sql/linux/LosaGame_Log_UNICODE.sql
docker exec -i mssql /opt/mssql-tools/bin/sqlcmd -S localhost -U sa -P '[REDACTED]' < /root/novera-legacy/sql/linux/LosaGame_UNICODE.sql
```

### 3.3 Obfuscation Password SQL untuk DBAgent
File `ls_dbagent_game.ini` dan `ls_dbagent_log.ini` membutuhkan password SQL ter-encode via double-XOR:
```bash
wine /root/kanna-server/ls_dbagent/ls_dbagent.exe -x "<password_plaintext>"
```
Salin string hex output ke baris `PW=` di dalam file INI.

---

## 4. Build Pipeline & CI (GitHub Actions)

Kompilasi source code server dan client menggunakan Windows runner (`windows-2022`) via GitHub Actions:
- **Workflow**: `.github/workflows/build.yml` di repository `https://github.com/necoffe/Legacy`
- **Build Automation**: `/var/www/ls/gh/ghbuild.ps1` (di-serve via `https://dl.kanna.web.id/gh/ghbuild.ps1`)
- **Trigger**: Otomatis saat push commit ke `main` atau manual via GitHub API `workflow_dispatch`:
  ```bash
  TOKEN=$(cat /root/.gh_token_novera | tr -d '\r\n ')
  curl -s -X POST -H "Authorization: token $TOKEN" \
       -H "Accept: application/vnd.github.v3+json" \
       https://api.github.com/repos/necoffe/Legacy/actions/workflows/build.yml/dispatches \
       -d '{"ref":"main"}'
  ```
- **Hasil Build**: Diunggah otomatis ke `/var/www/ls/gh-out/`:
  - `KannaClient-Build.zip` (Client `LostSaga.exe` + DLLs)
  - `KannaServer-Release.zip` (Seluruh executable server suite)

---

## 5. Konfigurasi Port & Jaringan

| Layanan | Protokol | Port | Keterangan |
|---|---|---|---|
| MSSQL Database | TCP | 1433 | Internal (127.0.0.1) |
| DBAgent (Log DB) | TCP | 30001 | Internal |
| DBAgent (Game DB) | TCP | 30101 | Internal / Service |
| DBAgent (Log) | TCP | 30102 | Internal / Service |
| Fake Billing / Billing Svr | TCP | 14011 | Internal |
| Main Server | TCP | 40000 | Akses Server |
| Login Server | TCP | 40005 | Publik (Client Auth) |
| Game Zone Server 1 | TCP/UDP | 20101 | Publik (Room & Gameplay) |
| Game Zone Server 2 | TCP/UDP | 20102 | Publik (Room & Gameplay) |
| Relay Server | UDP | 15001 | Publik (UDP Packet Tunneling) |
| Web Portal & Launcher | TCP | 80, 443 | Publik (`kanna.web.id`) |

---

## 6. Prosedur Menjalankan Server

### 6.1 Urutan Start Service (Wajib Sesuai Urutan):
1. **MSSQL Container**: `docker start mssql`
2. **Web / Registration Service**: `python3 /root/register-server.py`
3. **DB Agent (Log & Game)**:
   ```bash
   WINEPREFIX=/root/.wine-novera WINEDEBUG=-all wine /root/kanna-server/ls_dbagent/ls_dbagent.exe -g &
   WINEPREFIX=/root/.wine-novera WINEDEBUG=-all wine /root/kanna-server/ls_dbagent/ls_dbagent.exe -l &
   ```
4. **Billing Server**:
   ```bash
   WINEPREFIX=/root/.wine-novera WINEDEBUG=-all wine /root/kanna-server/ls_billingsvr/ls_billingsvr.exe &
   ```
5. **Main Server**:
   ```bash
   WINEPREFIX=/root/.wine-novera WINEDEBUG=-all wine /root/kanna-server/ls_mainsvr/ls_mainsvr.exe &
   ```
6. **Login Server**:
   ```bash
   WINEPREFIX=/root/.wine-novera WINEDEBUG=-all wine /root/kanna-server/ls_loginsvr/ls_loginsvr.exe &
   ```
7. **Game Zone Server**:
   ```bash
   WINEPREFIX=/root/.wine-novera WINEDEBUG=-all wine /root/kanna-server/ls_gamesvr/ls_gamesvr.exe &
   ```

### 6.2 Script All-in-One:
- Start all: `/root/start-kanna-server.sh`
- Stop all: `/root/stop-kanna-server.sh`
- Check status: `/root/status-kanna-server.sh`

---

## 7. Troubleshooting & Common Pitfalls

1. **ODBC / TLS 1.0 Connection Error**:
   Wine pada Debian modern membutuhkan library patch `secur32.so` (`:-VERS-ALL` -> `:+VERS-ALL`) agar mendukung TLS 1.0/1.1 legacy untuk koneksi ODBC MSSQL.
2. **IP Binding**:
   Seluruh file INI inter-service (`global_define.ini`, `config.ini`) harus menggunakan IP publik `172.232.251.28` (bukan 127.0.0.1) untuk komunikasi paket UDP/TCP client.
3. **Billingsvr Requirements**:
   `ls_billingsvr` membutuhkan file `global_define.ini` di direktori parent (`../global_define.ini`) dan parameter `TestMode=1`.
