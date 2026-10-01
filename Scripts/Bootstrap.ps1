$ErrorActionPreference = 'Stop'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'git is required and was not found on PATH'
}

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not (Test-Path '.git')) {
    git init --quiet
    if ($LASTEXITCODE -ne 0) { throw "git init failed with exit code $LASTEXITCODE" }
}

$env:GIT_TERMINAL_PROMPT = '0'
$env:GCM_INTERACTIVE = 'never'

$lockEntries = Get-Content 'Scripts/Vendor.lock'

$ErrorActionPreference = 'Continue'

$failed = @()
$unreachable = @()

foreach ($line in $lockEntries) {
    $line = $line.Trim()
    if ($line -eq '' -or $line.StartsWith('#')) { continue }

    $fields = $line -split '\s+'
    $name = $fields[0]
    $url = $fields[1]
    $ref = $fields[2]
    $path = "Vendor/$name"

    if (-not (Test-Path "$path/.git")) {
        Write-Host "==> adding $name"
        git submodule add --quiet --force --depth 1 $url $path
        if ($LASTEXITCODE -ne 0) {
            $failed += "$name  $url"
            continue
        }
        git config --file .gitmodules "submodule.$path.shallow" true
    }
    else {
        $currentUrl = git config --file .gitmodules --get "submodule.$path.url"
        if ($currentUrl -ne $url) {
            Write-Host "==> ${name}: remote is now $url"
            git config --file .gitmodules "submodule.$path.url" $url
            git submodule --quiet sync -- $path
        }
    }

    Write-Host "==> $name @ $ref"
    git -C $path fetch --quiet --depth 1 origin $ref
    if ($LASTEXITCODE -eq 0) {
        git -C $path -c advice.detachedHead=false checkout --quiet FETCH_HEAD
    }
    elseif ((git -C $path rev-parse HEAD) -eq $ref) {
        $unreachable += "$name  $url"
    }
    else {
        $failed += "$name  $url"
        continue
    }
    git add $path .gitmodules
}

Write-Host ''
if ($unreachable.Count -gt 0) {
    Write-Host 'Already at the pinned commit, but the remote could not be reached or does not have it:'
    foreach ($entry in $unreachable) { Write-Host "    $entry" }
    Write-Host 'Create or update these forks; until then a fresh clone of Trinity cannot fetch them.'
    Write-Host ''
}
if ($failed.Count -gt 0) {
    Write-Host 'Could not fetch:'
    foreach ($entry in $failed) { Write-Host "    $entry" }
    Write-Host 'Check that each fork exists and contains the pinned commit (see README, Dependencies).'
    exit 1
}

Write-Host 'Dependencies are in place. Next: cmake --preset <name>   (cmake --list-presets)'