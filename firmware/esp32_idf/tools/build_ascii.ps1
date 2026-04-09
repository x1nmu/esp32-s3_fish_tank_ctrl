param(
    [ValidateSet('build','reconfigure','fullclean')]
    [string]$Action = 'build'
)

$ErrorActionPreference = 'Stop'

$srcRoot = Split-Path -Parent $PSScriptRoot
$mirrorRoot = 'C:\temp\esp32_idf_ascii'

$idfPath = 'D:\v6.0\esp-idf'
$idfToolsPath = 'C:\Espressif\tools'
$venvPath = 'C:\Espressif\tools\python\v6.0\venv_repaired'
$pythonExe = Join-Path $venvPath 'Scripts\python.exe'
$idfPy = Join-Path $idfPath 'tools\idf.py'

$requiredPaths = @($idfPath, $idfToolsPath, $pythonExe, $idfPy)
foreach ($p in $requiredPaths) {
    if (-not (Test-Path $p)) {
        throw "Required path missing: $p"
    }
}

New-Item -ItemType Directory -Force -Path $mirrorRoot | Out-Null

# Mirror source tree to an ASCII-only path to avoid CMake crash on non-ASCII paths.
$null = robocopy $srcRoot $mirrorRoot /MIR /XD build .git .vscode

$env:IDF_PATH = $idfPath
$env:IDF_TOOLS_PATH = $idfToolsPath
$env:IDF_PYTHON_ENV_PATH = $venvPath
$env:PYTHON = $pythonExe
$env:ESP_IDF_VERSION = '6.0.0'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\esp-rom-elfs\20241011'
$env:PATH = "C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;$venvPath\Scripts;" + $env:PATH

Set-Location $mirrorRoot

switch ($Action) {
    'fullclean' {
        & $pythonExe $idfPy fullclean
    }
    'reconfigure' {
        & $pythonExe $idfPy reconfigure
    }
    'build' {
        & $pythonExe $idfPy reconfigure
        & $pythonExe $idfPy build
    }
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Completed action '$Action' in mirror path: $mirrorRoot"
