# Run the installed native engine with real media and capture ORT execution
# evidence. No Python, development environment or CUDA Toolkit is required.
param(
    [Parameter(Mandatory=$true)][string]$ModelDirectory,
    [Parameter(Mandatory=$true)][string]$Image,
    [Parameter(Mandatory=$true)][string]$Audio,
    [string]$OutputDirectory,
    [string]$RuntimeRoot = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'MossStudio\MossStudio\runtimes'),
    [ValidateRange(0.1,600)][double]$Seconds = 0.8,
    [switch]$CompareCpu,
    [switch]$CompareDirectML,
    [ValidateRange(30,7200)][int]$TimeoutSeconds = 1200
)
$ErrorActionPreference = 'Stop'
$worker = Join-Path $PSScriptRoot 'moss-ditto.exe'
if (-not (Test-Path -LiteralPath $worker -PathType Leaf)) { throw "Engine not found: $worker" }
$models = (Resolve-Path -LiteralPath $ModelDirectory).Path
$portrait = (Resolve-Path -LiteralPath $Image).Path
$speech = (Resolve-Path -LiteralPath $Audio).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path ([Environment]::GetFolderPath('Desktop')) ('XAvatar-GPU-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory to preserve previous evidence.' }
$root = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$utf8 = New-Object System.Text.UTF8Encoding($false)
$savedDevice = $env:XINSHEN_ONNX_DEVICE
$savedProfile = $env:XINSHEN_ONNX_PROFILE_DIR
$savedRuntime = $env:XINSHEN_CUDA_RUNTIME_DIR
$reports = @()
try {
    $runtimeManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cuda-runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $runtimeDirectory = Join-Path (Join-Path $RuntimeRoot 'sets') $runtimeManifest.id
    $controllers = @(Get-CimInstance Win32_VideoController)
    if (Test-Path -LiteralPath $runtimeDirectory -PathType Container) {
        foreach ($component in $runtimeManifest.components) {
            foreach ($file in $component.files) {
                if ((Get-FileHash -LiteralPath (Join-Path $runtimeDirectory $file.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'Cached GPU component verification failed.' }
            }
        }
        $env:XINSHEN_CUDA_RUNTIME_DIR = $runtimeDirectory
    } elseif (@($controllers | Where-Object { $_.Name -match 'NVIDIA' }).Count) {
        throw 'Open XAvatar and start a generation once to download GPU resources before running this test.'
    } else {
        $env:XINSHEN_CUDA_RUNTIME_DIR = $null
    }
    $controllers | Select-Object Name,DriverVersion,DriverDate |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $root 'hardware.json')
    # Record developer overrides so a forced CPU model cannot hide in the test.
    Get-ChildItem Env: | Where-Object { $_.Name -like 'XINSHEN_ONNX_DEVICE*' -or $_.Name -in @('XINSHEN_DML_DEVICE_ID','XINSHEN_CUDA_DEVICE_ID') } | Select-Object Name,Value |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $root 'device-environment.json')
    $modes = @('auto')
    if ($CompareDirectML) { $modes += 'directml' }
    if ($CompareCpu) { $modes += 'cpu' }
    foreach ($mode in $modes) {
        $run = (New-Item -ItemType Directory -Path (Join-Path $root $mode)).FullName
        $env:XINSHEN_ONNX_DEVICE = $mode
        $env:XINSHEN_ONNX_PROFILE_DIR = Join-Path $run 'profiles'
        $request = @{models=$models; image=$portrait; audio=$speech; directory=(Join-Path $run 'generation');
                     seed=1024; limitSeconds=$Seconds; mouthReference='source'}
        $requestPath = Join-Path $run 'request.json'
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
        $timer = [Diagnostics.Stopwatch]::StartNew()
        # Quote the path explicitly: Start-Process joins ArgumentList strings.
        $process = Start-Process -FilePath $worker -ArgumentList @('generate', ('"' + $requestPath + '"')) `
            -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $run 'stdout.log') `
            -RedirectStandardError (Join-Path $run 'engine.log')
        while (-not $process.WaitForExit(1000)) {
            if (Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue) {
                & nvidia-smi.exe --query-gpu=timestamp,index,utilization.gpu,memory.used --format=csv,noheader |
                    Add-Content -Encoding UTF8 (Join-Path $run 'gpu.csv')
            }
            if ($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                throw "$mode exceeded $TimeoutSeconds seconds"
            }
        }
        $process.WaitForExit()
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "$mode failed; inspect $run\engine.log" }
        $resultPath = Join-Path $run 'generation/result.json'
        $result = Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not (Test-Path -LiteralPath $result.path -PathType Leaf)) { throw "$mode did not produce a video" }
        $backends = @(Get-Content -LiteralPath (Join-Path $run 'engine.log') -Encoding UTF8 | ForEach-Object {
            try {
                $event = $_ | ConvertFrom-Json
                if ($event.event -eq 'backend' -and $event.phase -eq 'completed') { $event }
            } catch { }
        })
        $gpuExecuted = @($backends | Where-Object { $_.gpu -eq 'yes' -or $_.gpu -eq 'mixed' }).Count -gt 0
        $reports += [pscustomobject]@{mode=$mode; wallSeconds=$timer.Elapsed.TotalSeconds;
            gpuExecutionObserved=$gpuExecuted; gpuUtilization='not_measured_per_process';
            models=$backends; result=$result}
        if ($mode -ne 'cpu' -and -not $gpuExecuted) { throw 'No GPU kernel execution observed; inspect engine.log and profiles.' }
        if ($mode -eq 'cpu' -and $gpuExecuted) { throw 'CPU baseline unexpectedly executed GPU kernels.' }
    }
} finally {
    $env:XINSHEN_ONNX_DEVICE = $savedDevice
    $env:XINSHEN_ONNX_PROFILE_DIR = $savedProfile
    $env:XINSHEN_CUDA_RUNTIME_DIR = $savedRuntime
    [IO.File]::WriteAllText((Join-Path $root 'summary.json'), (ConvertTo-Json -InputObject @($reports) -Depth 30), $utf8)
    Write-Host "Evidence: $root"
}
Write-Host 'Completed. Compare model timings and inspect both videos for quality; device utilization samples are system-wide.'
