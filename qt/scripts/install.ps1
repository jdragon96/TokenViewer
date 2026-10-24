# Installs, updates or removes TokenViewer for the current user, without administrator rights (spec 5.3).
#   irm https://github.com/jdragon96/TokenViewer/releases/latest/download/install.ps1 | iex
#   & ([scriptblock]::Create((irm https://github.com/jdragon96/TokenViewer/releases/latest/download/install.ps1))) -Uninstall
#   .\install.ps1 -FromSource     build this checkout (package-windows.ps1) and install it
#   .\install.ps1 -Uninstall      remove the app, its Start Menu shortcut, login item, settings and logs
#   -NoLaunch                     do not start the app after installing
# Environment: TV_REPO=owner/name, TV_RELEASE_BASE=<url> (default: the latest GitHub release),
#              TV_INSTALL_DIR (default: %LOCALAPPDATA%\Programs\TokenViewer).
# Keep code outside string literals and comments ASCII: `irm` hands this file over decoded as Latin-1.
param(
    [switch]$FromSource,
    [switch]$Uninstall,
    [switch]$NoLaunch
)

try {
    # A child scope, so `irm | iex` leaves no functions or preferences behind in the caller's session.
    & {
        Set-StrictMode -Version 2.0
        $ErrorActionPreference = 'Stop'
        $ProgressPreference = 'SilentlyContinue'

        # The release workflow rewrites this line with the GitHub repository (owner/name).
        $ReleaseRepo = ''

        $AssetName = 'TokenViewer-windows.zip'
        $ExeName = 'TokenViewer.exe'
        $RunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        $RunValueName = 'TokenViewer'
        $SettingsKey = 'HKCU:\Software\TokenViewer'
        $DataDir = Join-Path $env:LOCALAPPDATA 'TokenViewer'
        $ShortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'TokenViewer.lnk'
        $InstallDir = if ($env:TV_INSTALL_DIR) { $env:TV_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'Programs\TokenViewer' }
        $InstallDir = [IO.Path]::GetFullPath($InstallDir)
        $InstalledExe = Join-Path $InstallDir $ExeName
        $QuitTimeoutSeconds = 5

        # GitHub serves release assets as application/octet-stream, which `irm` decodes as Latin-1: the UTF-8
        # Korean below then arrives as one character per byte, all at or below U+00FF. Turn those back into
        # the bytes and decode them as UTF-8. Text read correctly (Hangul is far above U+00FF) is left alone.
        function Repair-Text([string]$Text) {
            if ($Text -match '[\x80-\xFF]' -and $Text -notmatch '[^\x00-\xFF]') {
                return [Text.Encoding]::UTF8.GetString([Text.Encoding]::GetEncoding(28591).GetBytes($Text))
            }
            return $Text
        }

        function Write-Step([string]$Format, [object[]]$Values = @()) {
            Write-Host ('==> ' + ((Repair-Text $Format) -f $Values))
        }

        function Stop-Install([string]$Format, [object[]]$Values = @()) {
            throw ((Repair-Text $Format) -f $Values)
        }

        function Get-NativeOutput([string]$FilePath, [string[]]$Arguments) {
            $ErrorActionPreference = 'Continue'
            $output = & $FilePath @Arguments 2>$null
            if ($LASTEXITCODE -ne 0) {
                return ''
            }
            return "$output".Trim()
        }

        function Resolve-Repo {
            if ($env:TV_REPO) {
                return $env:TV_REPO
            }
            if ($ReleaseRepo) {
                return $ReleaseRepo
            }
            # Run from a checkout: the repository it was cloned from. Piped into iex there is no script folder.
            if ($PSScriptRoot -and (Get-Command git -ErrorAction SilentlyContinue)) {
                $url = Get-NativeOutput 'git' @('-C', $PSScriptRoot, 'remote', 'get-url', 'origin')
                if ($url -match 'github\.com[:/]([^/]+/[^/]+?)(\.git)?/?$') {
                    return $Matches[1]
                }
            }
            return ''
        }

        function Get-RunningApp {
            $running = @()
            foreach ($process in @(Get-Process -Name 'TokenViewer' -ErrorAction SilentlyContinue)) {
                try {
                    if ($process.Path -and ([IO.Path]::GetFullPath($process.Path) -ieq $InstalledExe)) {
                        $running += $process
                    }
                }
                catch {
                    # Another user's process: its path cannot be read, and it is not this install.
                }
            }
            return , $running
        }

        # The app's own --quit closes it like its quit button; one that does not exit in time is ended.
        function Stop-App {
            $running = Get-RunningApp
            if ($running.Count -eq 0) {
                return
            }
            Write-Step '실행 중인 TokenViewer 를 끝냅니다.'
            try {
                $quitter = Start-Process -FilePath $InstalledExe -ArgumentList '--quit' -WindowStyle Hidden -PassThru
                $quitter.WaitForExit($QuitTimeoutSeconds * 1000) | Out-Null
            }
            catch {
                # Ended below.
            }
            $deadline = (Get-Date).AddSeconds($QuitTimeoutSeconds)
            while ((Get-Date) -lt $deadline -and @($running | Where-Object { -not $_.HasExited }).Count -gt 0) {
                Start-Sleep -Milliseconds 100
            }
            foreach ($process in @($running | Where-Object { -not $_.HasExited })) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                $process.WaitForExit($QuitTimeoutSeconds * 1000) | Out-Null
            }
        }

        # TV_INSTALL_DIR may point anywhere: only an empty folder or one holding TokenViewer.exe is replaced or removed.
        function Test-InstallDirIsOurs {
            if (-not (Test-Path -LiteralPath $InstallDir)) {
                return $true
            }
            return (Test-Path -LiteralPath $InstalledExe) -or -not (Get-ChildItem -LiteralPath $InstallDir -Force | Select-Object -First 1)
        }

        function New-StartMenuShortcut {
            New-Item -ItemType Directory -Path (Split-Path -Parent $ShortcutPath) -Force | Out-Null
            $shell = New-Object -ComObject WScript.Shell
            try {
                $link = $shell.CreateShortcut($ShortcutPath)
                $link.TargetPath = $InstalledExe
                $link.WorkingDirectory = $InstallDir
                $link.IconLocation = "$InstalledExe,0"
                $link.Description = Repair-Text 'Claude 사용량'
                $link.Save()
            }
            finally {
                [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
            }
        }

        # Swaps the folder in: the old copy is renamed first, so a file still in use aborts before anything is lost.
        function Install-Folder([string]$Source) {
            if (-not (Test-Path -LiteralPath (Join-Path $Source $ExeName))) {
                Stop-Install '설치할 폴더에 {0} 이 없습니다: {1}' @($ExeName, $Source)
            }
            if (-not (Test-InstallDirIsOurs)) {
                Stop-Install '설치 폴더에 TokenViewer 가 아닌 파일이 있어 덮어쓰지 않습니다: {0}' @($InstallDir)
            }
            Stop-App
            $backup = "$InstallDir.old"
            if (Test-Path -LiteralPath $backup) {
                Remove-Item -LiteralPath $backup -Recurse -Force
            }
            $hadOld = Test-Path -LiteralPath $InstallDir
            if ($hadOld) {
                Rename-Item -LiteralPath $InstallDir -NewName (Split-Path -Leaf $backup)
            }
            try {
                New-Item -ItemType Directory -Path (Split-Path -Parent $InstallDir) -Force | Out-Null
                Copy-Item -LiteralPath $Source -Destination $InstallDir -Recurse
            }
            catch {
                if (Test-Path -LiteralPath $InstallDir) {
                    Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
                }
                if ($hadOld) {
                    Rename-Item -LiteralPath $backup -NewName (Split-Path -Leaf $InstallDir)
                }
                throw
            }
            if ($hadOld) {
                Remove-Item -LiteralPath $backup -Recurse -Force
            }
            Write-Step '설치했습니다: {0}' @($InstallDir)
            New-StartMenuShortcut
            Write-Step '시작 메뉴 바로가기: {0}' @($ShortcutPath)
            if ($NoLaunch) {
                Write-Step '실행하지 않았습니다 (-NoLaunch). 시작 메뉴에서 TokenViewer 를 여세요.'
            }
            else {
                Start-Process -FilePath $InstalledExe -WorkingDirectory $InstallDir
                Write-Step 'TokenViewer 를 실행했습니다.'
            }
            Write-Host (Repair-Text '    Windows 11 은 새 트레이 아이콘을 작업 표시줄 오른쪽 ^ 안에 숨깁니다.')
            Write-Host (Repair-Text '    ^ 를 눌러 TokenViewer 아이콘을 작업 표시줄로 끌어다 놓거나,')
            Write-Host (Repair-Text '    설정 → 개인 설정 → 작업 표시줄 → 기타 시스템 트레이 아이콘에서 TokenViewer 를 켜세요.')
        }

        function Save-Url([string]$Url, [string]$Path) {
            $client = New-Object Net.WebClient
            try {
                $client.DownloadFile($Url, $Path)
            }
            catch {
                $reason = if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message }
                Stop-Install '내려받지 못했습니다: {0} ({1})' @($Url, $reason)
            }
            finally {
                $client.Dispose()
            }
        }

        function Install-Release {
            $base = $env:TV_RELEASE_BASE
            if (-not $base) {
                $repo = Resolve-Repo
                if ($repo -notmatch '^[^/\s]+/[^/\s]+$') {
                    Stop-Install 'GitHub 저장소를 알 수 없습니다. $env:TV_REPO = "owner/TokenViewer" 로 지정하세요.'
                }
                $base = "https://github.com/$repo/releases/latest/download"
            }
            $base = $base.TrimEnd('/')
            # Windows PowerShell 5.1 on older .NET defaults may not offer TLS 1.2, which GitHub requires.
            $protocols = [int][Net.ServicePointManager]::SecurityProtocol
            if ($protocols -ne 0 -and ($protocols -band 3072) -eq 0) {
                [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]($protocols -bor 3072)
            }
            $work = Join-Path ([IO.Path]::GetTempPath()) ('TokenViewer-install-' + [Guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $work | Out-Null
            try {
                $zipPath = Join-Path $work $AssetName
                Write-Step '내려받는 중: {0}' @("$base/$AssetName")
                Save-Url "$base/$AssetName" $zipPath
                Save-Url "$base/$AssetName.sha256" "$zipPath.sha256"
                $expected = ([IO.File]::ReadAllText("$zipPath.sha256").Trim() -split '\s+')[0].ToLowerInvariant()
                $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath).Hash.ToLowerInvariant()
                if ($expected -notmatch '^[0-9a-f]{64}$' -or $expected -ne $actual) {
                    Stop-Install 'SHA-256 이 맞지 않습니다. 설치하지 않았습니다.'
                }
                Add-Type -AssemblyName System.IO.Compression.FileSystem
                $unzipped = Join-Path $work 'unzipped'
                [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $unzipped)
                Install-Folder (Join-Path $unzipped 'TokenViewer')
            }
            finally {
                Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

        function Install-FromSource {
            $packager = if ($PSScriptRoot) { Join-Path $PSScriptRoot 'package-windows.ps1' } else { '' }
            if (-not $packager -or -not (Test-Path -LiteralPath $packager)) {
                Stop-Install '-FromSource 는 저장소 안의 install.ps1 로 실행하세요.'
            }
            & $packager -SkipTests
            Install-Folder ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\dist\TokenViewer')))
        }

        function Uninstall-App {
            if (-not (Test-InstallDirIsOurs)) {
                Stop-Install '설치 폴더에 TokenViewer 가 아닌 파일이 있어 지우지 않습니다: {0}' @($InstallDir)
            }
            Stop-App
            $found = Test-Path -LiteralPath $InstallDir
            if (Get-ItemProperty -Path $RunKey -Name $RunValueName -ErrorAction SilentlyContinue) {
                Remove-ItemProperty -Path $RunKey -Name $RunValueName
                Write-Step '로그인 시 자동 실행을 껐습니다.'
            }
            if ($found) {
                Remove-Item -LiteralPath $InstallDir -Recurse -Force
                Write-Step '지웠습니다: {0}' @($InstallDir)
            }
            foreach ($path in @($ShortcutPath, $SettingsKey, $DataDir)) {
                if (Test-Path -LiteralPath $path) {
                    Remove-Item -LiteralPath $path -Recurse -Force
                    Write-Step '지웠습니다: {0}' @($path)
                }
            }
            if (-not $found) {
                Write-Step '설치된 앱이 없습니다. 남은 바로가기·설정·로그만 지웠습니다.'
            }
        }

        if ($Uninstall) {
            Uninstall-App
        }
        elseif ($FromSource) {
            Install-FromSource
        }
        else {
            Install-Release
        }
    }
}
catch {
    Write-Host "error: $($_.Exception.Message)" -ForegroundColor Red
    # Only a script file may exit: under iex, exit would close the caller's PowerShell window.
    if ($PSCommandPath) {
        exit 1
    }
}
