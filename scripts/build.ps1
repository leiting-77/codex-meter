param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path build,dist | Out-Null
    $compiler = (Get-Command g++ -ErrorAction Stop).Source
    $resourceCompiler = (Get-Command windres -ErrorAction Stop).Source
    & $resourceCompiler --include-dir src --include-dir assets src/app.rc -O coff -o build/app-resource.o
    if ($LASTEXITCODE -ne 0) { throw 'Icon resource compilation failed' }
    $common = @('-std=c++17','-O2','-s','-Wall','-Wextra','-Wno-missing-field-initializers','-DUNICODE','-D_UNICODE','-DNOMINMAX','-D_WIN32_WINNT=0x0A00','-finput-charset=UTF-8','-fexec-charset=UTF-8','-static','-save-temps=obj')
    & $compiler @common -municode -mwindows src/main.cpp build/app-resource.o -o build/codex_meter.exe -luser32 -lgdi32 -lshell32 -lole32 -luuid -lcomdlg32 -lcomctl32 -ldwmapi -lgdiplus
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed' }
    if (!$SkipTests) {
        & $compiler @common tests/model_tests.cpp -o build/model_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Model test compilation failed' }
        & ./build/model_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Model tests failed' }
        & $compiler @common tests/render_tests.cpp -o build/render_tests.exe -lgdi32 -luser32 -lshell32 -lole32 -luuid
        if ($LASTEXITCODE -ne 0) { throw 'Rendering test compilation failed' }
        & ./build/render_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Rendering tests failed' }
        & $compiler @common tests/mock_codex.cpp -o build/mock_codex.exe -lshell32 -lole32 -luuid
        if ($LASTEXITCODE -ne 0) { throw 'Mock compilation failed' }
        & $compiler @common tests/rpc_tests.cpp -o build/rpc_tests.exe -lshell32 -lole32 -luuid
        if ($LASTEXITCODE -ne 0) { throw 'RPC test compilation failed' }
        & ./build/rpc_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'RPC tests failed' }
    }
    Copy-Item -LiteralPath build/codex_meter.exe -Destination dist/codex_meter.exe -Force
    try {
        Copy-Item -LiteralPath build/codex_meter.exe -Destination codex_meter.exe -Force
    } catch {
        Write-Warning 'codex_meter.exe is currently running; the fresh binary is available in dist/codex_meter.exe.'
    }
    Get-Item dist/codex_meter.exe | Select-Object FullName,Length
} finally { Pop-Location }

