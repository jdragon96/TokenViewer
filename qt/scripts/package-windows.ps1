# Builds the Windows release into qt\dist (spec 5.2): Release build, tests, windeployqt, notices, zip,
# the Inno Setup installer, SHA-256s.
#   .\package-windows.ps1                  build, test and package
#   .\package-windows.ps1 -SkipTests       package without running ctest
#   .\package-windows.ps1 -SkipInstaller   zip only, without the installer (no Inno Setup needed)
#   .\package-windows.ps1 -QtDir C:\Qt\5.15.2\mingw81_64 -MinGWDir C:\Qt\Tools\mingw810_64
#   .\package-windows.ps1 -IsccPath 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
# Environment: TV_VERSION (default: the latest git tag without its v, then the CMake project version),
#              QT_ROOT_DIR or Qt5_DIR (set by jurplel/install-qt-action) when -QtDir is not given,
#              TV_ISCC (path of Inno Setup 6's ISCC.exe) when -IsccPath is not given.
param(
    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [string]$QtDir = '',
    [string]$MinGWDir = '',
    [string]$IsccPath = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$QtProjectDir = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent $QtProjectDir
$BuildDir = Join-Path $QtProjectDir 'build-release'
$DistDir = Join-Path $QtProjectDir 'dist'
$AppDir = Join-Path $DistDir 'TokenViewer'
$ZipName = 'TokenViewer-windows.zip'
$ZipPath = Join-Path $DistDir $ZipName
$ShaPath = "$ZipPath.sha256"
$SetupName = 'TokenViewer-windows-setup.exe'
$SetupPath = Join-Path $DistDir $SetupName
$SetupShaPath = "$SetupPath.sha256"
$SetupScript = Join-Path $QtProjectDir 'installer\TokenViewer.iss'
# Everything else windeployqt adds is dropped: the app draws its own icons (no image or icon plugins),
# forces the built-in Fusion style (no styles plugin) and does HTTPS through WinHTTP (no bearer plugin).
$KeptQtFiles = @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Network.dll', 'platforms\qwindows.dll')
$RuntimeFiles = @('libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll')

function Write-Step([string]$Message) {
    Write-Host "==> $Message"
}

# Runs a native tool and stops on a failed exit code. Its stderr is shown as plain text: Windows PowerShell
# would otherwise turn it into error records, which stop the script under ErrorActionPreference = Stop.
function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "실패했습니다 (종료 코드 $LASTEXITCODE): $FilePath $($Arguments -join ' ')"
    }
}

function Get-NativeOutput([string]$FilePath, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    $output = & $FilePath @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return $output
}

function Resolve-QtDir {
    if ($QtDir) {
        if (-not (Test-Path (Join-Path $QtDir 'bin\windeployqt.exe'))) {
            throw "-QtDir 에 Qt 가 없습니다 (bin\windeployqt.exe 없음): $QtDir"
        }
        return (Resolve-Path $QtDir).Path
    }
    $candidates = @()
    if ($env:QT_ROOT_DIR) {
        $candidates += $env:QT_ROOT_DIR
    }
    if ($env:Qt5_DIR) {
        # install-qt-action points Qt5_DIR at <Qt>\lib\cmake, older versions at <Qt> itself.
        $dir = $env:Qt5_DIR
        for ($i = 0; $i -lt 4 -and $dir; $i++) {
            $candidates += $dir
            $dir = Split-Path -Parent $dir
        }
    }
    $candidates += 'C:\Qt\5.15.2\mingw81_64'
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'bin\windeployqt.exe')) {
            return (Resolve-Path $candidate).Path
        }
    }
    throw 'Qt 5.15.2 (mingw81_64) 를 찾지 못했습니다. -QtDir <Qt 폴더> 를 주거나 QT_ROOT_DIR 를 설정하세요.'
}

# Only the MinGW 8.1 that Qt 5.15.2 was built with: another g++ on PATH would not match Qt's C++ ABI.
function Resolve-MinGWDir([string]$Qt) {
    if ($MinGWDir) {
        if (-not (Test-Path (Join-Path $MinGWDir 'bin\g++.exe'))) {
            throw "-MinGWDir 에 MinGW 가 없습니다 (bin\g++.exe 없음): $MinGWDir"
        }
        return (Resolve-Path $MinGWDir).Path
    }
    # Qt's installer and install-qt-action both put the tools in <root>\Tools next to <root>\5.15.2\mingw81_64.
    $candidates = @((Join-Path $Qt '..\..\Tools\mingw810_64'))
    if ($env:IQTA_TOOLS) {
        $candidates += Join-Path $env:IQTA_TOOLS 'mingw810_64'
    }
    $candidates += 'C:\Qt\Tools\mingw810_64'
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'bin\g++.exe')) {
            return (Resolve-Path $candidate).Path
        }
    }
    throw 'MinGW 8.1 (mingw810_64) 를 찾지 못했습니다. -MinGWDir <MinGW 폴더> 를 주세요.'
}

# Inno Setup 6's compiler. The official installer puts it under Program Files, or under
# %LOCALAPPDATA%\Programs when installed for the current user; the windows-2022 runner image has it preinstalled.
function Resolve-Iscc {
    $explicit = if ($IsccPath) { $IsccPath } else { $env:TV_ISCC }
    if ($explicit) {
        if (-not (Test-Path -LiteralPath $explicit -PathType Leaf)) {
            throw "ISCC.exe 가 없습니다: $explicit"
        }
        return (Resolve-Path -LiteralPath $explicit).Path
    }
    $candidates = @()
    $onPath = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($onPath) {
        $candidates += $onPath.Source
    }
    $candidates += Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    if (${env:ProgramFiles(x86)}) {
        $candidates += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    }
    $candidates += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Inno Setup 6 (ISCC.exe) 를 찾지 못했습니다. https://jrsoftware.org/isdl.php 에서 설치하거나 -IsccPath <ISCC.exe> 또는 TV_ISCC 를 주세요. 설치 프로그램 없이 만들려면 -SkipInstaller 를 쓰세요.'
}

function Get-AppVersion {
    $version = $env:TV_VERSION
    if (-not $version -and (Get-Command git -ErrorAction SilentlyContinue)) {
        $version = Get-NativeOutput 'git' @('-C', $RepoRoot, 'describe', '--tags', '--abbrev=0')
    }
    if (-not $version) {
        # CMake falls back to the project version.
        return ''
    }
    return ("$version".Trim() -replace '^v', '')
}

function Get-PeImports([string]$ObjDump, [string]$Path) {
    $imports = @()
    foreach ($line in (Get-NativeOutput $ObjDump @('-p', $Path))) {
        if ($line -match '^\s*DLL Name:\s*(\S+)') {
            $imports += $Matches[1]
        }
    }
    return $imports
}

# Every DLL the shipped binaries import must be in the folder or part of Windows.
function Assert-DependenciesPresent([string]$ObjDump) {
    $shipped = @{}
    Get-ChildItem -LiteralPath $AppDir -Recurse -File -Include '*.dll', '*.exe' | ForEach-Object { $shipped[$_.Name.ToLowerInvariant()] = $true }
    $system32 = Join-Path $env:SystemRoot 'System32'
    foreach ($binary in (Get-ChildItem -LiteralPath $AppDir -Recurse -File -Include '*.dll', '*.exe')) {
        $imports = @(Get-PeImports $ObjDump $binary.FullName)
        if ($imports.Count -eq 0) {
            throw "가져오는 DLL 목록을 읽지 못했습니다: $($binary.FullName)"
        }
        foreach ($import in $imports) {
            $name = $import.ToLowerInvariant()
            $isSystem = $name.StartsWith('api-ms-') -or $name.StartsWith('ext-ms-') -or (Test-Path (Join-Path $system32 $name))
            if (-not $shipped.ContainsKey($name) -and -not $isSystem) {
                throw "$($binary.Name) 이 쓰는 $import 이 배포 폴더에 없습니다."
            }
        }
    }
}

function Remove-UnusedDeployment {
    $keep = @{}
    foreach ($file in ($KeptQtFiles + $RuntimeFiles + @('TokenViewer.exe'))) {
        $keep[$file.ToLowerInvariant()] = $true
    }
    foreach ($item in (Get-ChildItem -LiteralPath $AppDir -Recurse -File)) {
        $relative = $item.FullName.Substring($AppDir.Length + 1).ToLowerInvariant()
        if (-not $keep.ContainsKey($relative)) {
            Write-Host "    지움: $relative"
            Remove-Item -LiteralPath $item.FullName -Force
        }
    }
    Get-ChildItem -LiteralPath $AppDir -Directory | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Recurse -File) } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
    foreach ($file in ($KeptQtFiles + $RuntimeFiles)) {
        if (-not (Test-Path (Join-Path $AppDir $file))) {
            throw "windeployqt 가 $file 을 넣지 않았습니다."
        }
    }
}

# The shipped Qt and runtime DLLs must be the Qt install's own release files.
function Assert-SameAsQt([string]$Qt) {
    foreach ($file in ($KeptQtFiles + $RuntimeFiles)) {
        $source = if ($file.Contains('\')) { Join-Path $Qt "plugins\$file" } else { Join-Path $Qt "bin\$file" }
        $shippedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $AppDir $file)).Hash
        if ($shippedHash -ne (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash) {
            throw "$file 이 $source 와 다릅니다."
        }
    }
}

function Copy-Notices([string]$MinGW) {
    Copy-Item -LiteralPath (Join-Path $QtProjectDir 'resources\THIRD_PARTY_NOTICES.md') -Destination $AppDir
    $licenses = Join-Path $AppDir 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    # Taken from the toolchain that built the release, so the texts match the shipped runtime.
    $sources = [ordered]@{
        'LGPL-3.0.txt'            = Join-Path $MinGW 'licenses\gcc\COPYING3.LIB'
        'GPL-3.0.txt'             = Join-Path $MinGW 'licenses\gcc\COPYING3'
        'GCC-exception-3.1.txt'   = Join-Path $MinGW 'licenses\gcc\COPYING.RUNTIME'
        'winpthreads-COPYING.txt' = Join-Path $MinGW 'licenses\winpthreads\COPYING'
        'TokenViewer-LICENSE.txt' = Join-Path $RepoRoot 'LICENSE'
    }
    foreach ($name in $sources.Keys) {
        if (-not (Test-Path -LiteralPath $sources[$name])) {
            throw "라이선스 전문이 없습니다: $($sources[$name])"
        }
        Copy-Item -LiteralPath $sources[$name] -Destination (Join-Path $licenses $name)
    }
}

# Entries use '/' and sit under TokenViewer\; Compress-Archive on Windows PowerShell writes '\' into entry names.
function New-ZipFromFolder([string]$SourceDir, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $root = Split-Path -Parent $SourceDir
    $stream = [IO.File]::Open($Destination, [IO.FileMode]::CreateNew)
    try {
        $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in (Get-ChildItem -LiteralPath $SourceDir -Recurse -File | Sort-Object FullName)) {
                $entryName = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        }
        finally {
            $zip.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

# Same format as shasum -a 256 (the macOS .sha256): hash, two spaces, file name, LF.
function Write-Sha256File([string]$Path) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$Path.sha256", "$hash  $(Split-Path -Leaf $Path)`n", (New-Object Text.UTF8Encoding($false)))
    return $hash
}

function Format-Kilobytes([long]$Bytes) {
    return ('{0:N0} KB' -f [math]::Ceiling($Bytes / 1KB))
}

$qt = Resolve-QtDir
$mingw = Resolve-MinGWDir $qt
foreach ($tool in @('cmake', 'ninja')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool 이 PATH 에 없습니다."
    }
}
# Before the build, so a missing Inno Setup fails in seconds rather than after it.
$iscc = if ($SkipInstaller) { '' } else { Resolve-Iscc }
$version = Get-AppVersion
$objdump = Join-Path $mingw 'bin\objdump.exe'
$originalPath = $env:PATH
$env:PATH = "$mingw\bin;$qt\bin;$env:PATH"
try {
    Write-Step "Qt: $qt"
    Write-Step "MinGW: $mingw"
    $mingwSlashed = $mingw.Replace('\', '/')
    Write-Step 'Release 구성·빌드'
    Invoke-Native 'cmake' @(
        '-S', $QtProjectDir, '-B', $BuildDir, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$($qt.Replace('\', '/'))",
        "-DCMAKE_CXX_COMPILER=$mingwSlashed/bin/g++.exe",
        "-DCMAKE_RC_COMPILER=$mingwSlashed/bin/windres.exe",
        "-DTV_VERSION=$version")
    Invoke-Native 'cmake' @('--build', $BuildDir)
    if ($SkipTests) {
        Write-Step '테스트 건너뜀 (-SkipTests)'
    }
    else {
        Write-Step '테스트'
        Invoke-Native 'ctest' @('--test-dir', $BuildDir, '--output-on-failure')
    }

    Write-Step "배포 폴더: $AppDir"
    foreach ($path in @($AppDir, $ZipPath, $ShaPath, $SetupPath, $SetupShaPath)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Path $AppDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $BuildDir 'TokenViewer.exe') -Destination $AppDir
    # --debug, not --release: for MinGW, windeployqt calls a binary "release" only when its PE header says the
    # debug info was stripped. Qt's own MinGW plugins do not say so, and --release would find no platform plugin.
    # Qt 5.15.2's MinGW build ships release DLLs only, so these are the same files (checked below).
    Invoke-Native (Join-Path $qt 'bin\windeployqt.exe') @(
        '--debug', '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw', '--no-angle', '--compiler-runtime',
        (Join-Path $AppDir 'TokenViewer.exe'))
    Write-Step '쓰지 않는 Qt 파일 정리'
    Remove-UnusedDeployment
    Assert-SameAsQt $qt
    Assert-DependenciesPresent $objdump
    Copy-Notices $mingw

    Write-Step "압축: $ZipPath"
    New-ZipFromFolder $AppDir $ZipPath
    $hash = Write-Sha256File $ZipPath

    # The version the app shows (the exe's version info), so the setup and Apps & features say the same.
    $shownVersion = (Get-Item -LiteralPath (Join-Path $AppDir 'TokenViewer.exe')).VersionInfo.ProductVersion
    if (-not $SkipInstaller) {
        if (-not $shownVersion) {
            throw 'TokenViewer.exe 에 버전 정보가 없습니다.'
        }
        Write-Step "설치 프로그램: $SetupPath ($iscc)"
        Invoke-Native $iscc @(
            "/DAppVersion=$shownVersion", "/DSourceDir=$AppDir", "/DIconFile=$(Join-Path $BuildDir 'TokenViewer.ico')",
            "/O$DistDir", $SetupScript)
        if (-not (Test-Path -LiteralPath $SetupPath)) {
            throw "ISCC 가 $SetupName 을 만들지 않았습니다."
        }
        $setupHash = Write-Sha256File $SetupPath
    }

    $files = Get-ChildItem -LiteralPath $AppDir -Recurse -File | Sort-Object FullName
    foreach ($file in $files) {
        Write-Host ('    {0,10}  {1}' -f (Format-Kilobytes $file.Length), $file.FullName.Substring($AppDir.Length + 1))
    }
    $folderBytes = ($files | Measure-Object -Property Length -Sum).Sum
    Write-Host "TokenViewer $shownVersion : 폴더 $(Format-Kilobytes $folderBytes), $ZipName $(Format-Kilobytes (Get-Item -LiteralPath $ZipPath).Length)"
    Write-Host "SHA-256: $hash"
    if (-not $SkipInstaller) {
        Write-Host "$SetupName $(Format-Kilobytes (Get-Item -LiteralPath $SetupPath).Length)"
        Write-Host "SHA-256: $setupHash"
    }
}
finally {
    $env:PATH = $originalPath
}
