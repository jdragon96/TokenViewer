# Installs, updates or removes TokenViewer for the current user, without administrator rights (spec 5.3).
#   irm https://github.com/jdragon96/TokenViewer/releases/latest/download/install.ps1 | iex
#   & ([scriptblock]::Create((irm https://github.com/jdragon96/TokenViewer/releases/latest/download/install.ps1))) -Uninstall
#   .\install.ps1 -FromSource     build this checkout (package-windows.ps1) and install it
#   .\install.ps1 -Uninstall      remove the app, its Start Menu shortcut, login item, settings and logs
#   -NoLaunch                     do not start the app after installing
# Installing runs the release's installer (TokenViewer-windows-setup.exe) silently, so this route and the
# installer leave the same install: an Apps & features entry with its uninstaller.
# Environment: TV_REPO=owner/name, TV_RELEASE_BASE=<url> (default: the latest GitHub release),
#              TV_INSTALL_DIR (default: where it is already installed, else %LOCALAPPDATA%\Programs\TokenViewer).
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

        $AssetName = 'TokenViewer-windows-setup.exe'
        $ExeName = 'TokenViewer.exe'
        # Written by the installer for its AppId (qt\installer\TokenViewer.iss).
        $UninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{CFD0D0E6-2DEE-4F55-95EB-05DE69CA8A73}_is1'
        $RunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        $RunValueName = 'TokenViewer'
        $SettingsKey = 'HKCU:\Software\TokenViewer'
        $DataDir = Join-Path $env:LOCALAPPDATA 'TokenViewer'
        $ShortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'TokenViewer.lnk'
        $DefaultInstallDir = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\TokenViewer'))
        $InstallDir = if ($env:TV_INSTALL_DIR) { [IO.Path]::GetFullPath($env:TV_INSTALL_DIR) } else { $DefaultInstallDir }
        $SetupLog = Join-Path ([IO.Path]::GetTempPath()) 'TokenViewer-setup.log'
        $SilentSwitches = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
        $QuitTimeoutSeconds = 5
        $UninstallTimeoutSeconds = 30

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

        # The folder the installer recorded, or '' when it has not installed the app.
        function Get-InstalledAppDir {
            $entry = Get-ItemProperty -LiteralPath $UninstallKey -ErrorAction SilentlyContinue
            if ($entry -and $entry.PSObject.Properties['Inno Setup: App Path']) {
                return "$($entry.'Inno Setup: App Path')"
            }
            return ''
        }

        function Get-RunningApp([string]$Exe) {
            $running = @()
            foreach ($process in @(Get-Process -Name 'TokenViewer' -ErrorAction SilentlyContinue)) {
                try {
                    if ($process.Path -and ([IO.Path]::GetFullPath($process.Path) -ieq $Exe)) {
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
        function Stop-App([string]$Exe) {
            $running = Get-RunningApp $Exe
            if ($running.Count -eq 0) {
                return
            }
            Write-Step '실행 중인 TokenViewer 를 끝냅니다.'
            try {
                $quitter = Start-Process -FilePath $Exe -ArgumentList '--quit' -WindowStyle Hidden -PassThru
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

        # The folders this script installs into and may remove as a whole: the default one and TV_INSTALL_DIR.
        function Test-ManagedDir([string]$Dir) {
            $full = [IO.Path]::GetFullPath($Dir).TrimEnd('\')
            return ($full -ieq $DefaultInstallDir.TrimEnd('\')) -or ($full -ieq $InstallDir.TrimEnd('\'))
        }

        # Guards a folder this script installs into or removes (the default one or TV_INSTALL_DIR, which may point
        # anywhere): it must be empty or hold TokenViewer.exe.
        function Test-InstallDirIsOurs([string]$Dir) {
            if (-not (Test-Path -LiteralPath $Dir)) {
                return $true
            }
            return (Test-Path -LiteralPath (Join-Path $Dir $ExeName)) -or -not (Get-ChildItem -LiteralPath $Dir -Force | Select-Object -First 1)
        }

        # The installer closes a copy running from its folder, replaces the files and creates the Start Menu
        # shortcut and the Apps & features entry. An update stays in its folder unless TV_INSTALL_DIR is set.
        function Install-Setup([string]$SetupPath) {
            if (($env:TV_INSTALL_DIR -or -not (Get-InstalledAppDir)) -and -not (Test-InstallDirIsOurs $InstallDir)) {
                Stop-Install '설치 폴더에 TokenViewer 가 아닌 파일이 있어 덮어쓰지 않습니다: {0}' @($InstallDir)
            }
            $arguments = $SilentSwitches + @('/CURRENTUSER', ('/LOG="{0}"' -f $SetupLog))
            if ($env:TV_INSTALL_DIR) {
                $arguments += '/DIR="{0}"' -f $InstallDir
            }
            Write-Step '설치 프로그램을 실행합니다.'
            try {
                $process = Start-Process -FilePath $SetupPath -ArgumentList $arguments -Wait -PassThru
            }
            catch {
                # e.g. Smart App Control refusing an unsigned program.
                Stop-Install '설치 프로그램을 실행하지 못했습니다: {0}' @($_.Exception.Message)
            }
            if ($process.ExitCode -ne 0) {
                Stop-Install '설치 프로그램이 실패했습니다 (종료 코드 {0}). 기록: {1}' @($process.ExitCode, $SetupLog)
            }
            Remove-Item -LiteralPath $SetupLog -ErrorAction SilentlyContinue
            $appDir = Get-InstalledAppDir
            if (-not $appDir) {
                Stop-Install '설치 프로그램이 끝났지만 설치 정보가 없습니다.'
            }
            Write-Step '설치했습니다: {0}' @($appDir)
            Write-Step '시작 메뉴 바로가기: {0}' @($ShortcutPath)
            if ($NoLaunch) {
                Write-Step '실행하지 않았습니다 (-NoLaunch). 시작 메뉴에서 TokenViewer 를 여세요.'
            }
            else {
                Start-Process -FilePath (Join-Path $appDir $ExeName) -WorkingDirectory $appDir
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
                $setupPath = Join-Path $work $AssetName
                Write-Step '내려받는 중: {0}' @("$base/$AssetName")
                Save-Url "$base/$AssetName" $setupPath
                Save-Url "$base/$AssetName.sha256" "$setupPath.sha256"
                $expected = ([IO.File]::ReadAllText("$setupPath.sha256").Trim() -split '\s+')[0].ToLowerInvariant()
                $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $setupPath).Hash.ToLowerInvariant()
                if ($expected -notmatch '^[0-9a-f]{64}$' -or $expected -ne $actual) {
                    Stop-Install 'SHA-256 이 맞지 않습니다. 설치하지 않았습니다.'
                }
                Install-Setup $setupPath
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
            Install-Setup ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\dist\$AssetName")))
        }

        # The installer's own uninstaller closes the app and removes its files, shortcut and Apps & features entry.
        function Uninstall-Setup {
            $entry = Get-ItemProperty -LiteralPath $UninstallKey
            $appDir = Get-InstalledAppDir
            $command = if ($entry.PSObject.Properties['UninstallString']) { "$($entry.UninstallString)" } else { '' }
            $uninstaller = if ($command -match '^\s*"([^"]+)"') { $Matches[1] } else { $command.Trim() }
            if (-not $uninstaller -or -not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
                # The uninstaller was deleted by hand, and with it the list of files it installed. Only a folder this
                # script manages is removed as a whole (before the entry, so a refused folder keeps its entry);
                # any other folder the installer was pointed at may be shared with other programs and is left alone.
                $dir = if ($appDir) { $appDir } else { $InstallDir }
                $leaveDir = (Test-Path -LiteralPath $dir) -and -not (Test-ManagedDir $dir)
                if (-not $leaveDir) {
                    Write-Step '제거 프로그램이 없어 직접 지웁니다: {0}' @($dir)
                    Uninstall-Folder $dir | Out-Null
                }
                Remove-Item -LiteralPath $UninstallKey -Recurse -Force
                Write-Step '설치 정보를 지웠습니다.'
                if ($leaveDir) {
                    Write-Step '제거 프로그램이 없어 이 폴더는 지우지 않았습니다. 다른 파일이 함께 있을 수 있으니 확인하고 직접 지우세요: {0}' @($dir)
                }
                return
            }
            Write-Step '제거 프로그램을 실행합니다: {0}' @($appDir)
            try {
                $process = Start-Process -FilePath $uninstaller -ArgumentList $SilentSwitches -Wait -PassThru
            }
            catch {
                # e.g. Smart App Control refusing an unsigned program.
                Stop-Install '제거 프로그램을 실행하지 못했습니다: {0}' @($_.Exception.Message)
            }
            if ($process.ExitCode -ne 0) {
                Stop-Install '제거 프로그램이 실패했습니다 (종료 코드 {0}).' @($process.ExitCode)
            }
            # The uninstaller hands the work to a copy of itself, which deletes the uninstaller last.
            $deadline = (Get-Date).AddSeconds($UninstallTimeoutSeconds)
            while ((Get-Date) -lt $deadline -and ((Test-Path -LiteralPath $UninstallKey) -or (Test-Path -LiteralPath $uninstaller))) {
                Start-Sleep -Milliseconds 200
            }
            if (Test-Path -LiteralPath $UninstallKey) {
                Stop-Install '제거가 끝나지 않았습니다: {0}' @($appDir)
            }
            if ($appDir -and (Test-Path -LiteralPath $appDir)) {
                # Settings and logs stay too: the running copy holds its lock file under them.
                if ((Get-RunningApp (Join-Path $appDir $ExeName)).Count -gt 0) {
                    Stop-Install 'TokenViewer 가 실행 중이라 설치 폴더를 다 지우지 못했습니다. TokenViewer 를 끝내고 다시 제거하세요: {0}' @($appDir)
                }
                Write-Step '제거했지만 설치 폴더에 다른 파일이 남아 있어 폴더는 두었습니다: {0}' @($appDir)
            }
            else {
                Write-Step '지웠습니다: {0}' @($appDir)
            }
        }

        # Removes a folder this script manages: a zip install from an older install.ps1, or an install whose
        # uninstaller is gone.
        function Uninstall-Folder([string]$Dir) {
            if (-not (Test-InstallDirIsOurs $Dir)) {
                Stop-Install '설치 폴더에 TokenViewer 가 아닌 파일이 있어 지우지 않습니다: {0}' @($Dir)
            }
            Stop-App (Join-Path $Dir $ExeName)
            if (-not (Test-Path -LiteralPath $Dir)) {
                return $false
            }
            Remove-Item -LiteralPath $Dir -Recurse -Force
            Write-Step '지웠습니다: {0}' @($Dir)
            return $true
        }

        function Uninstall-App {
            if (Test-Path -LiteralPath $UninstallKey) {
                Uninstall-Setup
                $found = $true
            }
            else {
                $found = Uninstall-Folder $InstallDir
            }
            if (Get-ItemProperty -Path $RunKey -Name $RunValueName -ErrorAction SilentlyContinue) {
                Remove-ItemProperty -Path $RunKey -Name $RunValueName
                Write-Step '로그인 시 자동 실행을 껐습니다.'
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
