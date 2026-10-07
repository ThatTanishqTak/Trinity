$ErrorActionPreference = 'Stop'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'git is required and was not found on PATH'
}

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:GIT_TERMINAL_PROMPT = '0'
$env:GCM_INTERACTIVE = 'never'

$destination = 'Trinity-Forge/Tests/Models'
$lockEntries = Get-Content 'Scripts/Samples.lock'

$ErrorActionPreference = 'Continue'

$failed = @()

# Each entry is a repository of its own under the destination, holding only the listed paths, and only their files are downloaded
foreach ($line in $lockEntries) {
    $line = $line.Trim()
    if ($line -eq '' -or $line.StartsWith('#')) { continue }

    $fields = $line -split '\s+'
    $name = $fields[0]
    $url = $fields[1]
    $ref = $fields[2]
    $patterns = $fields[3..($fields.Count - 1)] | ForEach-Object { "/$_" }
    $path = "$destination/$name"

    Write-Host "==> $name @ $ref"
    if (-not (Test-Path "$path/.git")) {
        New-Item -ItemType Directory -Force -Path $path | Out-Null
        git init --quiet $path
        git -C $path remote add origin $url
    }
    else {
        git -C $path remote set-url origin $url
    }

    git -C $path sparse-checkout set --no-cone @patterns
    if ($LASTEXITCODE -eq 0) {
        git -C $path fetch --quiet --depth 1 --filter=blob:none origin $ref
    }
    if ($LASTEXITCODE -eq 0) {
        git -C $path -c advice.detachedHead=false checkout --quiet --force FETCH_HEAD
    }
    if ($LASTEXITCODE -ne 0) {
        $failed += "$name  $url"
    }
}

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host 'Could not fetch:'
    foreach ($entry in $failed) { Write-Host "    $entry" }
    exit 1
}

Write-Host "Samples are in $destination. Next: Trinity-Forge --import-test"