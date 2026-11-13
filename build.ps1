# build.ps1 - builds the benchmark on Windows and picks up every sort file in sorts\ automatically.
#
#   .\build.ps1                   build and run the full benchmark
#   .\build.ps1 -MaxN 10000       build and run a quick benchmark
#   .\build.ps1 -BuildOnly        compile without running
#   .\build.ps1 -Test             compile and run regression tests only
#   .\build.ps1 -NoOpen           build and run without opening the CSV folder
#
# If PowerShell blocks the script, run it like this instead:
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Run -MaxN 10000

param(
    [switch]$Run,
    [int]$MaxN = 100000,
    [switch]$Extended,
    [switch]$Test,
    [switch]$BuildOnly,
    [switch]$NoOpen
)

$ErrorActionPreference = "Stop"
if ($BuildOnly -and $Run) {
    Write-Error "Choose -BuildOnly or -Run, not both."
    exit 1
}
$shouldRun = $Run -or (-not $BuildOnly -and -not $Test)
$buildFlags = @("-O2", "-std=c++17", "-Wall", "-Wextra", "-static")

Set-Location $PSScriptRoot

# 1. Find g++ (PATH first, then the usual MSYS2 location)
$gpp = $null
$cmd = Get-Command g++ -ErrorAction SilentlyContinue
if ($cmd) { $gpp = $cmd.Source }
elseif (Test-Path "C:\msys64\ucrt64\bin\g++.exe") { $gpp = "C:\msys64\ucrt64\bin\g++.exe" }
if (-not $gpp) {
    Write-Host "ERROR: g++ not found. Install MSYS2 (msys2.org) and its ucrt64 g++, or add g++ to PATH." -ForegroundColor Red
    exit 1
}
Write-Host "Using compiler: $gpp"
# Load runtime DLLs from the selected compiler, not an older Git installation.
$env:PATH = "$(Split-Path -Parent $gpp);$env:PATH"

# 2. Collect sort files: sorts\<name>_sort.cpp  ->  -DHAVE_<NAME>
$known   = @("bubble","selection","insertion","shell","merge","counting","bucket")
$sources = @("benchmark.cpp", "searches.cpp")
$defs    = @()
$found   = @()
if (Test-Path "sorts") {
    foreach ($f in (Get-ChildItem "sorts" -Filter "*_sort.cpp")) {
        $name = $f.BaseName -replace '_sort$', ''
        if ($known -notcontains $name.ToLower()) {
            Write-Host "WARNING: sorts\$($f.Name) skipped - expected one of: $($known -join ', ') followed by _sort.cpp" -ForegroundColor Yellow
            continue
        }
        $defs    += "-DHAVE_" + $name.ToUpper()
        $sources += "sorts/" + $f.Name
        $found   += $name
    }
}
if ($found.Count -eq 0) { Write-Host "No sort files found in sorts\ yet - building searches only." }
else { Write-Host "Sorts included: $($found -join ', ')" }
$missing = $known | Where-Object { $found -notcontains $_ }
if ($missing) { Write-Host "Not yet present: $($missing -join ', ')" }

# 3. Build
Write-Host "Building..."
& $gpp @buildFlags @defs @sources -o benchmark.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED - fix the errors above (they name the file and line)." -ForegroundColor Red
    exit 1
}
Write-Host "Build OK -> benchmark.exe (generates benchmark CSVs)" -ForegroundColor Green

foreach ($testName in @("data_gen_tests", "benchmark_tests")) {
    & $gpp @buildFlags -Wpedantic -Werror "$testName.cpp" searches.cpp -o "$testName.exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "Build OK -> $testName.exe (correctness tests, not report CSV generation)" -ForegroundColor Green
}

if ($Test) {
    foreach ($testName in @("data_gen_tests", "benchmark_tests")) {
        & (Join-Path $PSScriptRoot "$testName.exe")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

if ($shouldRun) {
    $suite = if ($Extended) { "extended" } else { "required" }
    $runName = "{0}-{1}-{2}" -f $suite, (Get-Date -Format "yyyyMMdd-HHmmss-fff"), ([guid]::NewGuid().ToString("N").Substring(0, 8))
    $runDir = Join-Path (Join-Path $PSScriptRoot "results") $runName
    New-Item -ItemType Directory -Path $runDir | Out-Null
    $benchmarkArgs = @("$MaxN")
    if ($Extended) { $benchmarkArgs += "--extended" }
    $inputFiles = @($sources) + @("metrics.h", "data_gen.h", "searches.h", "sorts.h", "build.ps1", "benchmark.exe")
    $hashes = @{}
    foreach ($path in $inputFiles) { $hashes[$path] = (Get-FileHash -Algorithm SHA256 $path).Hash }
    $cpu = Get-CimInstance Win32_Processor
    $os = Get-CimInstance Win32_OperatingSystem
    $info = [ordered]@{
        started_utc = [DateTime]::UtcNow.ToString("o")
        compiler = $gpp
        compiler_version = (& $gpp --version) -join "`n"
        flags = $buildFlags + $defs
        os = $os.Caption + " " + $os.Version
        cpu = ($cpu.Name -join "; ")
        ram_bytes = [uint64]$os.TotalVisibleMemorySize * 1024
        arguments = $benchmarkArgs
        registered_sorts = @($found)
        sha256 = $hashes
        methodology = "Instrumented time; generation/copies/validation/precheck excluded; search-result capture included. Five search batches; median/min/max batch means; matched query counts. Sort trials: 15 for N<=1000, otherwise 3. No dedicated warm-up. See CSV for seed and per-row counts."
    }
    $infoPath = Join-Path $runDir "run_info.json"
    $info | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $infoPath
    Write-Host "Running benchmark (maxN=$MaxN). Results: $runDir"
    $executable = Join-Path $PSScriptRoot "benchmark.exe"
    Push-Location $runDir
    try {
        & $executable @benchmarkArgs | Tee-Object -FilePath "execution_log.txt"
        $runExitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $info["finished_utc"] = [DateTime]::UtcNow.ToString("o")
    $info["exit_code"] = $runExitCode
    $info | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $infoPath
    if ($runExitCode -ne 0) { exit $runExitCode }
    Write-Host "Done. Saved CSVs, execution_log.txt, and run_info.json in $runDir" -ForegroundColor Green
    Write-Host "Open $(Join-Path $runDir 'search_results.csv') in Excel (CSV, not .xlsx)."
    if (-not $NoOpen) {
        try { Invoke-Item -LiteralPath $runDir }
        catch { Write-Warning "CSV files are saved, but File Explorer could not open. Use the folder above." }
    }
} elseif ($BuildOnly) {
    Write-Host "Build only: no results generated. Run .\build.ps1 or double-click benchmark.exe to generate CSVs."
}
