param([ValidatePattern('^\d+\.\d+\.\d+(\.\d+)?$')][string]$Version = '1.0.0')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent

# Prefer the PATH, then the standard per-user and system installations.
$innoCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
$innoCompiler = if ($innoCommand) { $innoCommand.Source } else { $null }
if (!$innoCompiler) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    $innoCompiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$innoCompiler) {
    throw 'Inno Setup 6 not found. Install it with: winget install --id JRSoftware.InnoSetup -e -s winget'
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $innoCompiler "/DAppVersion=$Version" (Join-Path $projectRoot 'installer\CloseAllSystray.iss')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Installer built successfully: $projectRoot\dist\CloseAllSystray-Setup.exe"
