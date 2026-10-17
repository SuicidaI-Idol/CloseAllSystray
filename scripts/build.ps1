param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$compiler = 'C:\msys64\mingw64\bin\g++.exe'
if (!(Test-Path -LiteralPath $compiler)) {
    $compiler = (Get-Command g++.exe -ErrorAction Stop).Source
}
$env:PATH = "$(Split-Path $compiler -Parent);$env:PATH"
$outputDir = Join-Path $projectRoot "build\$Configuration"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$options = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-municode', '-mwindows',
    '-static', '-D_WIN32_WINNT=0x0A00', '-finput-charset=UTF-8', '-fexec-charset=UTF-8')
if ($Configuration -eq 'Debug') { $options += @('-g3', '-O0') }
else { $options += @('-O2', '-DNDEBUG') }
$resourceCompiler = Join-Path (Split-Path $compiler -Parent) 'windres.exe'
& $resourceCompiler '-I' (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'src\app.rc') '-O' 'coff' '-o' (Join-Path $outputDir 'app.res.o')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $compiler @options (Join-Path $projectRoot 'src\main.cpp') (Join-Path $outputDir 'app.res.o') '-o' (Join-Path $outputDir 'CloseAllSystray.exe') '-lcomctl32' '-lole32' '-lshell32' '-luuid' '-ladvapi32' '-ldwmapi'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Build succeeded: $outputDir\CloseAllSystray.exe"
