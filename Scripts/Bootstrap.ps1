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
# KTX-Software keeps its test images in Git LFS, and nothing here needs them
$env:GIT_LFS_SKIP_SMUDGE = '1'

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

# A NuGet package pinned in a lock file as "<id> <version> <SHA-256>", downloaded with a hash check and unpacked into Vendor/<folder>/<version>, unless the file that shows it is there exists already
function Get-PinnedPackage([string]$lockFile, [string]$id, [string]$label, [string]$folder, [string]$marker) {
    $fields = (Get-Content $lockFile | Where-Object { $_ -match "^$([regex]::Escape($id)) " }) -split '\s+'
    $version = $fields[1]
    $expected = $fields[2]
    $path = Join-Path $root "Vendor/$folder/$version"

    if (Test-Path (Join-Path $path $marker)) {
        Write-Host "==> $label @ $version (already downloaded)"
        return
    }

    Write-Host "==> $label @ $version"
    $name = $id.ToLowerInvariant()
    $package = Join-Path ([System.IO.Path]::GetTempPath()) "$name.$version.nupkg"
    try {
        Invoke-WebRequest -UseBasicParsing -ErrorAction Stop -Uri "https://api.nuget.org/v3-flatcontainer/$name/$version/$name.$version.nupkg" -OutFile $package
        $hash = (Get-FileHash -Algorithm SHA256 $package).Hash
        if ($hash -ne $expected) {
            $script:failed += "$label $version  SHA-256 is $hash, expected $expected"
        }
        else {
            if (Test-Path $path) { Remove-Item -Recurse -Force $path }
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [System.IO.Compression.ZipFile]::ExtractToDirectory($package, $path)
        }
    }
    catch {
        $script:failed += "$label $version  $($_.Exception.Message)"
    }
    finally {
        Remove-Item -ErrorAction SilentlyContinue $package
    }
}

Get-PinnedPackage 'Scripts/AgilitySDK.lock' 'Microsoft.Direct3D.D3D12' 'Agility SDK' 'AgilitySDK' 'build/native/bin/x64/D3D12Core.dll'
Get-PinnedPackage 'Scripts/GameInput.lock' 'Microsoft.GameInput' 'GameInput' 'GameInput' 'native/include/GameInput.h'

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
    Write-Host 'Check that each fork exists and contains the pinned commit, and that nuget.org is reachable (see README, Dependencies).'
    exit 1
}

Write-Host 'Dependencies are in place. Next: cmake --preset <name>   (cmake --list-presets)'