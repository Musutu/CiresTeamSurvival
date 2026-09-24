[CmdletBinding()]
param(
    [switch]$PrepareOnly,
    [string]$InstallPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
)

$ErrorActionPreference = 'Stop'
$stagingPath = Join-Path $env:TEMP 'CiresTeamSurvival-BuildTools'
$installerPath = Join-Path $stagingPath 'vs_BuildTools-17.14.41.exe'
$statusPath = Join-Path $stagingPath 'setup-status.json'
# Official Microsoft binary; URL/hash resolved from winget on 2026-09-23.
$installerUrl = 'https://download.visualstudio.microsoft.com/download/pr/bc92e2cb-33de-4a0c-995d-efa817f16b16/37bb0fb429d163ecebd272a865d11a37b906d152bef960da2ddb29c2e2fd6eeb/vs_BuildTools.exe'
$expectedHash = '37BB0FB429D163ECEBD272A865D11A37B906D152BEF960DA2DDB29C2E2FD6EEB'
New-Item -ItemType Directory -Path $stagingPath -Force | Out-Null

function Write-SetupStatus([string]$State, [object]$Details) {
    [ordered]@{
        timestamp = (Get-Date).ToString('o')
        state = $State
        installer = $installerPath
        installPath = $InstallPath
        details = $Details
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statusPath -Encoding utf8
    Write-Host "Setup status: $State ($statusPath)"
}

if (-not (Test-Path -LiteralPath $installerPath)) {
    Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath
}
$actualHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
$signature = Get-AuthenticodeSignature -LiteralPath $installerPath
if ($actualHash -ne $expectedHash -or $signature.Status -ne 'Valid' -or
    $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    Write-SetupStatus 'installer-verification-failed' @{ sha256 = $actualHash; signature = $signature.Status.ToString() }
    throw 'Installer hash or Microsoft signature did not match. Installation was not started.'
}

$isAdministrator = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($PrepareOnly) {
    Write-SetupStatus 'prepared' @{ sha256 = $actualHash; signature = $signature.Status.ToString(); administrator = $isAdministrator }
    return
}
if (-not $isAdministrator) {
    Write-SetupStatus 'administrator-required' 'Open PowerShell as administrator and run this script. It does not request elevation or change security settings.'
    throw 'Visual Studio Build Tools requires an administrator console. Installation was not started.'
}
if ($InstallPath.Contains('"')) { throw 'InstallPath must not contain double quotes.' }

# Use the exact supported compiler family from UE5.8 Windows_SDK.json.
# --norestart prevents automatic reboot; required workload dependencies are included.
$installerArguments = @(
    '--quiet', '--wait', '--norestart', '--nocache',
    '--installPath', ('"' + $InstallPath + '"'),
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--add', 'Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621',
    '--add', 'Microsoft.Net.Component.4.8.SDK',
    '--add', 'Microsoft.Net.Component.4.8.TargetingPack'
)
if (Test-Path -LiteralPath (Join-Path $InstallPath 'Common7\Tools\LaunchDevCmd.bat')) {
    $installerArguments = @('modify') + $installerArguments
}
Write-SetupStatus 'installing' @{ components = @('MSVC v143 14.44', 'Windows SDK 10.0.22621.0', '.NET Framework 4.8 SDK and targeting pack') }
$installer = Start-Process -FilePath $installerPath -ArgumentList $installerArguments -WindowStyle Hidden -PassThru -Wait
if ($installer.ExitCode -notin @(0, 3010)) {
    Write-SetupStatus 'installation-failed' @{ exitCode = $installer.ExitCode; logs = (Join-Path $env:TEMP 'dd_*') }
    throw "Visual Studio installer returned $($installer.ExitCode). See dd_* logs in the temporary directory."
}

$compiler = Get-ChildItem -LiteralPath (Join-Path $InstallPath 'VC\Tools\MSVC') -Directory |
    Where-Object { $_.Name -match '^14\.44\.' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $compiler) { throw 'Installer finished but a supported MSVC 14.44 compiler was not found.' }
$compilerPath = Join-Path $compiler.FullName 'bin\Hostx64\x64\cl.exe'
# Servicing updates retain the original toolset folder name. Check cl.exe itself.
$compilerVersion = [version](Get-Item -LiteralPath $compilerPath).VersionInfo.ProductVersion
if ($compilerVersion -lt [version]'14.44.35211') { throw "Unsupported compiler patch: $compilerVersion" }
$sdkRoot = @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots') |
    ForEach-Object { (Get-ItemProperty -LiteralPath $_ -ErrorAction SilentlyContinue).KitsRoot10 } |
    Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Lib\10.0.22621.0\um\x64\kernel32.lib')) } |
    Select-Object -First 1
if (-not $sdkRoot) { throw 'Windows SDK 10.0.22621.0 libraries were not found.' }
$sdkLibrary = Join-Path $sdkRoot 'Lib\10.0.22621.0\um\x64\kernel32.lib'
if (-not (Test-Path -LiteralPath $compilerPath) -or -not (Test-Path -LiteralPath $sdkLibrary)) {
    throw 'Compiler or Windows SDK verification failed after installation.'
}
$netFxRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\NETFXSDK\4.8' -ErrorAction SilentlyContinue).KitsInstallationFolder
if (-not $netFxRoot -or -not (Test-Path -LiteralPath (Join-Path $netFxRoot 'Include\um\mscoree.h'))) {
    throw '.NET Framework 4.8 SDK required by Unreal SwarmInterface was not found.'
}
Write-SetupStatus 'ready' @{ compiler = $compilerPath; compilerVersion = $compilerVersion.ToString(); sdk = $sdkRoot; netFxSdk = $netFxRoot; restartRequired = ($installer.ExitCode -eq 3010) }
Write-Host "Compiler ready: $compilerPath"
if ($installer.ExitCode -eq 3010) { Write-Host 'Windows reports a restart is needed. This script has not restarted the PC.' }
