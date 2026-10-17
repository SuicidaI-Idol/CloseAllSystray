$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$compiler = 'C:\msys64\mingw64\bin\g++.exe'
$env:PATH = "$(Split-Path $compiler -Parent);$env:PATH"
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force build\integration | Out-Null
    $flags = @('-std=c++17', '-g', '-O0', '-Wall', '-Wextra', '-municode', '-static', '-D_WIN32_WINNT=0x0A00', '-finput-charset=UTF-8', '-fexec-charset=UTF-8')
    & $compiler @flags '-mwindows' tests/fixture.cpp -o build/integration/fixture.exe '-lshell32'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $compiler @flags tests/integration.cpp -o build/integration/integration.exe '-lcomctl32' '-lole32' '-lshell32' '-luuid' '-ladvapi32' '-lgdi32' '-ldwmapi'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\build\integration\integration.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
