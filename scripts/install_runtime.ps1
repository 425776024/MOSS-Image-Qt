param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$Manifest,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$component = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $component.sha256) { throw 'Archive checksum mismatch' }
$root = [IO.Path]::GetFullPath($Destination)
if (!(Test-Path -LiteralPath $root -PathType Container) -or @(Get-ChildItem -LiteralPath $root -Force).Count) { throw 'Expected empty staging directory' }
$expected = @{}
foreach ($file in $component.files) {
    $name = [string]$file.path
    if ($name -notmatch '^[A-Za-z0-9_./-]+$' -or $name -match '(^|/)(\.|\.\.|)(/|$)' -or $name -match '\.(/|$)' -or $expected.ContainsKey($name)) { throw 'Unsafe manifest path' }
    $expected[$name] = $file
}
$archiveFile = [IO.Compression.ZipFile]::OpenRead($Archive)
try {
    if ($archiveFile.Entries.Count -ne $expected.Count) { throw 'Unexpected archive members' }
    $seen = @{}
    foreach ($entry in $archiveFile.Entries) {
        $name = $entry.FullName
        if (!$expected.ContainsKey($name) -or $seen.ContainsKey($name)) { throw 'Unexpected archive member' }
        $seen[$name] = $true
        $type = ($entry.ExternalAttributes -shr 16) -band 61440
        if ($type -ne 0 -and $type -ne 32768) { throw 'Links and special files are forbidden' }
        $file = $expected[$name]
        if ($entry.Length -ne [long]$file.bytes) { throw 'File size mismatch' }
        $target = [IO.Path]::GetFullPath((Join-Path $root $name))
        if (!$target.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path outside staging directory' }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        $inputStream = $entry.Open()
        try {
            $outputStream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
        } finally { $inputStream.Dispose() }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'File checksum mismatch' }
    }
} finally { $archiveFile.Dispose() }
