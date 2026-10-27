$ErrorActionPreference = "Continue"
Write-Host "=== BUILD IOFREETYPE PIPELINE ==="
Write-Host ("pwd: " + (Get-Location).Path)

# 1. Download and extract novera-libs.zip to get LSLog.lib and other libs
Write-Host "`n### 1. DOWNLOAD LIBS ###"
New-Item -ItemType Directory -Force -Path "lib" | Out-Null
New-Item -ItemType Directory -Force -Path "_libsx" | Out-Null

$libsUrl = "https://dl.kanna.web.id/gh/libs/novera-libs.zip"
Write-Host "Downloading novera-libs.zip..."
$curl = Join-Path $env:SystemRoot "System32\curl.exe"
if (Test-Path $curl) {
  & $curl -sSL --retry 3 --retry-delay 2 -o "libs.zip" $libsUrl
} else {
  Invoke-WebRequest -UseBasicParsing -Uri $libsUrl -OutFile "libs.zip"
}

if (Test-Path "libs.zip") {
  Write-Host ("libs.zip size: " + [int]((Get-Item libs.zip).Length/1MB) + " MB")
  Expand-Archive -Path "libs.zip" -DestinationPath "_libsx" -Force
  if (Test-Path "_libsx\lib") {
    Copy-Item "_libsx\lib\*" "lib\" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ("Copied libs to lib\. Total libs: " + (Get-ChildItem lib -Filter *.lib).Count)
  }
}

# 2. Check or build freetype2312.lib
Write-Host "`n### 2. PREPARE FREETYPE 2.3.12 LIB ###"
New-Item -ItemType Directory -Force -Path "src\ioFreeType\FreeType\Lib" | Out-Null

$foundFt = Get-ChildItem -Recurse -Filter "freetype2312.lib" -Path "_libsx","lib" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($foundFt) {
  Write-Host ("Found existing freetype2312.lib at: " + $foundFt.FullName)
  Copy-Item $foundFt.FullName "src\ioFreeType\FreeType\Lib\freetype2312.lib" -Force
  Copy-Item $foundFt.FullName "lib\freetype2312.lib" -Force
} else {
  Write-Host "freetype2312.lib not found in libs.zip. Building FreeType 2.3.12 from source..."
  Invoke-WebRequest -UseBasicParsing -Uri "https://downloads.sourceforge.net/project/freetype/freetype2/2.3.12/ft2312.zip" -OutFile ft2312.zip
  Expand-Archive -Path ft2312.zip -DestinationPath _ft -Force
  $ftSrc = (Get-ChildItem _ft -Recurse -Directory -Filter "freetype-2.3.12" | Select-Object -First 1).FullName
  Write-Host ("FreeType source dir: " + $ftSrc)

  # Find MSVC x86 cl.exe and lib.exe
  $cl = $null; $libExe = $null; $chosenMsvc = $null
  foreach ($b in @("C:\Program Files\Microsoft Visual Studio\2022\Enterprise","C:\Program Files\Microsoft Visual Studio\2022\Community","C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools")) {
    $c = Join-Path $b "VC\Tools\MSVC"
    if (Test-Path $c) {
      $ver = Get-ChildItem $c -Directory | Sort-Object Name -Descending | Select-Object -First 1
      $cl = Join-Path $ver.FullName "bin\HostX64\x86\cl.exe"
      $libExe = Join-Path $ver.FullName "bin\HostX64\x86\lib.exe"
      $chosenMsvc = $ver.FullName
      if (Test-Path $cl) { break }
    }
  }
  Write-Host ("Using cl: " + $cl)

  # Setup INCLUDE and PATH
  $env:PATH = ((Join-Path $chosenMsvc "bin\HostX64\x86") + ";" + $env:PATH)
  $incL = @((Join-Path $chosenMsvc "include"))
  $sdkIncRoot = "C:\Program Files (x86)\Windows Kits\10\Include"
  if (Test-Path $sdkIncRoot) {
    $sdkVer = (Get-ChildItem $sdkIncRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
    foreach ($sub in @("ucrt","um","shared")) {
      $d = Join-Path $sdkVer $sub
      if (Test-Path $d) { $incL += $d }
    }
  }
  $env:INCLUDE = ($incL -join ";")

  $ftFiles = @(
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
  Push-Location $ftSrc
  foreach ($f in $ftFiles) {
    & $cl /nologo /c /MD /O2 /W0 /Iinclude /DFT2_BUILD_LIBRARY /DWIN32 /D_LIB $f 2>&1 | Out-Null
  }
  $ftObjs = Get-ChildItem -Recurse -Filter "*.obj" -File | ForEach-Object { $_.FullName }
  Write-Host ("Compiled FreeType object files: " + $ftObjs.Count)
  Pop-Location

  $repoRoot = (Get-Location).Path
  $ftLibOut = (Join-Path $repoRoot "src\ioFreeType\FreeType\Lib\freetype2312.lib")
  & $libExe /nologo "/OUT:$ftLibOut" @ftObjs
  Copy-Item $ftLibOut (Join-Path $repoRoot "lib\freetype2312.lib") -Force
  Write-Host ("freetype2312.lib created: " + (Test-Path $ftLibOut))
}

# 3. Check LSLog.lib
Write-Host "`n### 3. CHECK LSLOG.LIB ###"
Write-Host ("lib\LSLog.lib exists: " + (Test-Path "lib\LSLog.lib"))
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
  $f = Get-ChildItem "C:\Program Files*" -Recurse -Filter "MSBuild.exe" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "Current.Bin" } | Select-Object -First 1
  if ($f) { $msbuild = $f.FullName }
}
Write-Host ("Using MSBuild: " + $msbuild)

if (-not (Test-Path "lib\LSLog.lib")) {
  Write-Host "Building LSLog.vcxproj..."
  & $msbuild src\LSLog\LSLog.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0 /m /v:m 2>&1
}

# 4. Build ioFreeType.vcxproj
Write-Host "`n### 4. BUILD IOFREETYPE.VCXPROJ ###"
$ftVcx = "src\ioFreeType\ioFreeType.vcxproj"
& $msbuild $ftVcx /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0 /m /v:m 2>&1 | Tee-Object -FilePath "msb-freetype.log"

Write-Host "`n### 5. VERIFY OUTPUT ###"
if (Test-Path "lib\ioFreeType.dll") {
  $outDll = Get-Item "lib\ioFreeType.dll"
  Write-Host ("SUCCESS: lib\ioFreeType.dll (" + [int]($outDll.Length/1KB) + " KB, " + $outDll.LastWriteTime + ")")
} else {
  Write-Host "FAILURE: lib\ioFreeType.dll was NOT created!"
  Get-Content "msb-freetype.log" | Select-String -Pattern "error" | ForEach-Object { Write-Host $_.Line }
  exit 1
}
