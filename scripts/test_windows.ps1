# Collect hardware and run the deployed app; no admin or Python required.
param(
    [string]$ModelRoot,
    [switch]$Inference,
    [switch]$CpuVoice
)
$ErrorActionPreference = 'Stop'
$appRoot = $PSScriptRoot
$reportRoot = Join-Path ([Environment]::GetFolderPath('Desktop')) ("Moss-Test-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $reportRoot | Out-Null
Start-Transcript -Path (Join-Path $reportRoot 'test.log') | Out-Null
$results = @()
$failed = $false
$env:MOSS_DISABLE_DEV_PATHS = '1'
$env:MOSS_EVIDENCE_ROOT = $reportRoot
$env:MOSS_OUTPUT_ROOT = Join-Path $reportRoot 'works'
$env:MOSS_SETTINGS_ROOT = Join-Path $reportRoot 'settings'
if ($ModelRoot) { $env:MOSS_MODEL_ROOT = (Resolve-Path $ModelRoot).Path }
if ($CpuVoice) { $env:MOSS_VOICE_BACKEND = 'cpu' } else { $env:MOSS_VOICE_BACKEND = 'vulkan' }

function Invoke-MossTest([string]$Name, [string[]]$Arguments, [int]$Timeout = 120) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath (Join-Path $appRoot 'XAvatar.exe') -ArgumentList $Arguments -PassThru `
        -RedirectStandardOutput (Join-Path $reportRoot "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $reportRoot "$Name.stderr.log")
    while (-not $process.WaitForExit(1000)) {
        if (Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue) {
            & nvidia-smi.exe --query-gpu=timestamp,utilization.gpu,memory.used,power.draw --format=csv,noheader |
                Add-Content -Encoding UTF8 (Join-Path $reportRoot "$Name.gpu.csv")
        }
        if ($timer.Elapsed.TotalSeconds -gt $Timeout) {
            & taskkill.exe /PID $process.Id /T /F | Out-Null
            throw "$Name exceeded $Timeout seconds"
        }
    }
    $process.Refresh()
    $script:results += [pscustomobject]@{name=$Name; exitCode=$process.ExitCode; seconds=$timer.Elapsed.TotalSeconds}
    if ($process.ExitCode -ne 0) { throw "$Name failed with exit code $($process.ExitCode)" }
}

try {
    Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $reportRoot 'windows.json')
    Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, DriverDate, VideoProcessor |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $reportRoot 'gpu.json')
    # WMI AdapterRAM truncates >4 GiB; use the vendor tool for a 24GB 3090.
    if (Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue) {
        & nvidia-smi.exe --query-gpu=name,driver_version,memory.total --format=csv |
            Set-Content -Encoding UTF8 (Join-Path $reportRoot 'nvidia.csv')
    }
    Invoke-MossTest 'public-package' @('--headless','--public-release-check')
    foreach ($language in @('zh','en')) {
        foreach ($theme in @('light','dark')) {
            Invoke-MossTest "home-$language-$theme" @('--home-smoke','--ui-language',$language,'--ui-theme',$theme)
        }
    }
    if ($Inference) {
        if (-not $ModelRoot) { throw 'Use -ModelRoot with -Inference; download models in the App first.' }
        # Start with voice design: no portrait or personal recording required.
        Invoke-MossTest 'ming-voice' @('--voice-design-test') 600
        Invoke-MossTest 'moss-image' @('--generate-test','--quant','Q4_K','--size','512','--steps','8') 1200
    }
    Write-Host 'Tests passed. GPU backend and output quality still require inspection of engine.log and media.'
} catch {
    $failed = $true
    $_ | Out-String | Set-Content -Encoding UTF8 (Join-Path $reportRoot 'failure.txt')
    Write-Host "Test stopped: $_"
} finally {
    ConvertTo-Json -InputObject @($results) -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $reportRoot 'results.json')
    Stop-Transcript | Out-Null
    Compress-Archive -Path "$reportRoot/*" -DestinationPath "$reportRoot.zip"
    Write-Host "Report: $reportRoot.zip"
}
if ($failed) { exit 1 }
