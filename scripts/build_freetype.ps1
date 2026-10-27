param(
  [switch]$PrepareLib
)

$ErrorActionPreference = "Continue"
$repoRoot = (Get-Location).Path
Write-Host "=== FreeType / ioFreeType Build Helper ==="
Write-Host ("Repo root: " + $repoRoot)

$ftLibDir = Join-Path $repoRoot "src\ioFreeType\FreeType\Lib"
New-Item -ItemType Directory -Force -Path $ftLibDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $repoRoot "lib") | Out-Null

$targetLib = Join-Path $ftLibDir "freetype2312.lib"
if (Test-Path $targetLib) {
  Write-Host ("freetype2312.lib already exists: " + $targetLib)
  Copy-Item $targetLib (Join-Path $repoRoot "lib\freetype2312.lib") -Force -ErrorAction SilentlyContinue
  exit 0
}

# 1. Locate ft2312.zip
$zipPath = Join-Path $repoRoot "src\ioFreeType\FreeType\ft2312.zip"
if (-not (Test-Path $zipPath)) {
  Write-Host "ft2312.zip not found locally, downloading..."
  $curl = Join-Path $env:SystemRoot "System32\curl.exe"
  if (Test-Path $curl) {
    & $curl -sSL --retry 3 --retry-delay 2 -o $zipPath "https://downloads.sourceforge.net/project/freetype/freetype2/2.3.12/ft2312.zip"
  } else {
    Invoke-WebRequest -UseBasicParsing -Uri "https://downloads.sourceforge.net/project/freetype/freetype2/2.3.12/ft2312.zip" -OutFile $zipPath
  }
}

if (-not (Test-Path $zipPath)) {
  Write-Host "ERROR: ft2312.zip could not be found or downloaded."
  exit 1
}

# 2. Extract ft2312.zip
$ftExtract = Join-Path $repoRoot "_ft_build"
Remove-Item $ftExtract -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -Path $zipPath -DestinationPath $ftExtract -Force

$ftSrcDir = (Get-ChildItem $ftExtract -Recurse -Directory -Filter "freetype-2.3.12" | Select-Object -First 1).FullName
Write-Host ("FreeType source located at: " + $ftSrcDir)

# 3. Locate MSVC x86 tools (cl.exe, lib.exe)
$cl = $null; $libExe = $null; $chosenMsvc = $null
foreach ($b in @("C:\Program Files\Microsoft Visual Studio\2022\Enterprise","C:\Program Files\Microsoft Visual Studio\2022\Community","C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools","C:\Program Files\Microsoft Visual Studio")) {
  $c = Join-Path $b "VC\Tools\MSVC"
  if (Test-Path $c) {
    $ver = Get-ChildItem $c -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if ($ver) {
      $cl = Join-Path $ver.FullName "bin\HostX64\x86\cl.exe"
      $libExe = Join-Path $ver.FullName "bin\HostX64\x86\lib.exe"
      $chosenMsvc = $ver.FullName
      if (Test-Path $cl) { break }
    }
  }
}

if (-not $cl -or -not (Test-Path $cl)) {
  $f = Get-ChildItem "C:\Program Files*" -Recurse -Filter "cl.exe" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "HostX64.x86" } | Select-Object -First 1
  if ($f) {
    $cl = $f.FullName
    $libExe = Join-Path (Split-Path $cl) "lib.exe"
    $chosenMsvc = Split-Path (Split-Path (Split-Path $cl))
  }
}

Write-Host ("Using cl.exe: " + $cl)
Write-Host ("Using lib.exe: " + $libExe)

# Setup INCLUDE path
$incList = @()
if ($chosenMsvc) {
  $incList += (Join-Path $chosenMsvc "include")
}
$sdkIncRoot = "C:\Program Files (x86)\Windows Kits\10\Include"
if (Test-Path $sdkIncRoot) {
  $sdkVer = (Get-ChildItem $sdkIncRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
  foreach ($sub in @("ucrt","um","shared")) {
    $d = Join-Path $sdkVer $sub
    if (Test-Path $d) { $incList += $d }
  }
}
$env:INCLUDE = ($incList -join ";")
$env:PATH = ((Split-Path $cl) + ";" + $env:PATH)

# 4. Compile FreeType C files
$cFiles = @(
  "src\autofit\autofit.c", "src\bdf\bdf.c", "src\cff\cff.c", "src\base\ftbase.c",
  "src\base\ftbitmap.c", "src\cache\ftcache.c", "src\base\ftfstype.c", "src\base\ftgasp.c",
  "src\base\ftglyph.c", "src\gzip\ftgzip.c", "src\base\ftinit.c", "src\lzw\ftlzw.c",
  "src\base\ftstroke.c", "src\base\ftsystem.c", "src\smooth\smooth.c", "src\base\ftbbox.c",
  "src\base\ftmm.c", "src\base\ftpfr.c", "src\base\ftsynth.c", "src\base\fttype1.c",
  "src\base\ftwinfnt.c", "src\base\ftlcdfil.c", "src\base\ftgxval.c", "src\base\ftotval.c",
  "src\base\ftpatent.c", "src\pcf\pcf.c", "src\pfr\pfr.c", "src\psaux\psaux.c",
  "src\pshinter\pshinter.c", "src\psnames\psmodule.c", "src\raster\raster.c",
  "src\sfnt\sfnt.c", "src\truetype\truetype.c", "src\type1\type1.c", "src\cid\type1cid.c",
  "src\type42\type42.c", "src\winfonts\winfnt.c"
)

Push-Location $ftSrcDir
foreach ($f in $cFiles) {
  & $cl /nologo /c /MD /O2 /W0 /Iinclude /DFT2_BUILD_LIBRARY /DWIN32 /D_LIB $f 2>&1 | Out-Null
}
$objs = Get-ChildItem -Recurse -Filter "*.obj" -File | ForEach-Object { $_.FullName }
Write-Host ("Compiled FreeType obj files: " + $objs.Count)
Pop-Location

# 5. Create freetype2312.lib
& $libExe /nologo "/OUT:$targetLib" @objs
if (Test-Path $targetLib) {
  Write-Host ("SUCCESS: Created " + $targetLib + " (" + [int]((Get-Item $targetLib).Length/1KB) + " KB)")
  Copy-Item $targetLib (Join-Path $repoRoot "lib\freetype2312.lib") -Force
  Copy-Item $targetLib (Join-Path $ftLibDir "freetype2312_D.lib") -Force
  Copy-Item $targetLib (Join-Path $repoRoot "lib\freetype2312_D.lib") -Force
} else {
  Write-Host "ERROR: Failed to create freetype2312.lib"
  exit 1
}

# Clean temp objects
Remove-Item $ftExtract -Recurse -Force -ErrorAction SilentlyContinue
