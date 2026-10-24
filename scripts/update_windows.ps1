param(
    [Parameter(Mandatory=$true)][string]$Target,
    [Parameter(Mandatory=$true)][string]$Work,
    [Parameter(Mandatory=$true)][int]$ParentId,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{32}$')][string]$Token
)
$ErrorActionPreference = 'Stop'
$Target = [IO.Path]::GetFullPath($Target).TrimEnd('\')
$Work = [IO.Path]::GetFullPath($Work).TrimEnd('\')
if ($ParentId -le 1 -or $Work -notmatch ('\\app-updates\\' + $Token + '$')) { throw 'Invalid transaction' }
if (!(Test-Path -LiteralPath (Join-Path $Work 'pending.json'))) { throw 'Missing transaction' }
$pending = Get-Content -LiteralPath (Join-Path $Work 'pending.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ([IO.Path]::GetFullPath([string]$pending.target).TrimEnd('\') -ne $Target -or $pending.token -ne $Token) { throw 'Target mismatch' }
$manifestPath = Join-Path $Target 'update-files.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.product -ne 'ai.ffclip.studio' -or $manifest.files -notcontains 'XAvatar.exe') { throw 'Invalid installation' }
$owned = @{}
foreach ($file in $manifest.files) {
    if ([IO.Path]::IsPathRooted($file) -or $file -match '(^|[\\/])\.\.([\\/]|$)' -or $file.Contains(':')) { throw 'Invalid manifest path' }
    $owned[$file.Replace('/', '\')] = $true
}
# Never traverse a user's junction or symlink when backing up and cleaning.
$entries = @(Get-ChildItem -LiteralPath $Target -Recurse -Force)
if ((Get-Item -LiteralPath $Target).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation' }
foreach ($entry in $entries) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation file' }
}
$backup = "$Target.previous-$Token"
if (Test-Path -LiteralPath $backup) { throw 'Backup already exists' }
New-Item -ItemType File -Path (Join-Path $Work 'helper-ready') | Out-Null
$process = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
for ($attempt=0; $process -and !$process.HasExited; $attempt++) {
    if (Test-Path -LiteralPath (Join-Path $Work 'cancel.json')) { Remove-Item -LiteralPath $Work -Recurse -Force; exit 0 }
    if ($attempt -ge 150) { throw 'Application did not close' }
    Start-Sleep -Seconds 2
}
if (Test-Path -LiteralPath (Join-Path $Work 'cancel.json')) { Remove-Item -LiteralPath $Work -Recurse -Force; exit 0 }
$renamed = $false
$confirmed = $false
$newProcess = $null
try {
    Move-Item -LiteralPath $Target -Destination $backup
    $renamed = $true
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCANCEL /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /DIR="' + $Target + '" /LOG="' + (Join-Path $Work 'installer.log') + '"'
    $installer = Start-Process -FilePath (Join-Path $Work 'package.exe') -ArgumentList $arguments -Wait -PassThru
    if ($installer.ExitCode -ne 0) { throw "Installer failed: $($installer.ExitCode)" }
    if (!(Test-Path -LiteralPath (Join-Path $Target 'XAvatar.exe'))) { throw 'Application missing' }
    # Preserve files the user added inside the install directory. Only files in
    # the previous distribution manifest are redundant application files.
    foreach ($file in $entries | Where-Object { !$_.PSIsContainer }) {
        $relative = $file.FullName.Substring($Target.Length + 1)
        if ($owned.ContainsKey($relative) -or $relative -match '^unins\d+\.(exe|dat|msg)$') { continue }
        $destination = Join-Path $Target $relative
        if (Test-Path -LiteralPath $destination) { $destination = Join-Path $Target ("Update-preserved-$Token\" + $relative) }
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $backup $relative) -Destination $destination
    }
    $newProcess = Start-Process -FilePath (Join-Path $Target 'XAvatar.exe') -WorkingDirectory $Target -ArgumentList @('--complete-update', $Token) -PassThru
    $started = $false
    for ($attempt=0; $attempt -lt 45; $attempt++) {
        Start-Sleep -Seconds 1
        if ($newProcess.HasExited) { break }
        if (Test-Path -LiteralPath (Join-Path $Work 'started.json')) { $started=$true; break }
    }
    if (!$started) { throw 'Updated application failed to start' }
    Start-Sleep -Seconds 2
    if ($newProcess.HasExited) { throw 'Updated application exited during startup' }
    $confirmed = $true
    Remove-Item -LiteralPath $backup -Recurse -Force
    Remove-Item -LiteralPath $Work -Recurse -Force
    Write-Output 'Update installed; previous files and downloaded installer removed.'
} catch {
    $failure = $_
    if ($confirmed) { Write-Warning "Update succeeded; cleanup needs retry: $failure"; exit 0 }
    if ($newProcess -and !$newProcess.HasExited) { $newProcess.Kill(); $newProcess.WaitForExit() }
    if ($renamed -and (Test-Path -LiteralPath $backup)) {
        if (Test-Path -LiteralPath $Target) { Remove-Item -LiteralPath $Target -Recurse -Force }
        Move-Item -LiteralPath $backup -Destination $Target
        Start-Process -FilePath (Join-Path $Target 'XAvatar.exe') -WorkingDirectory $Target
        Remove-Item -LiteralPath $Work -Recurse -Force
    } elseif (!$renamed -and (Test-Path -LiteralPath (Join-Path $Target 'XAvatar.exe'))) {
        Start-Process -FilePath (Join-Path $Target 'XAvatar.exe') -WorkingDirectory $Target
    }
    throw $failure
}
