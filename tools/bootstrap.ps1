param([string]$Destination = "$PSScriptRoot/../third_party/REFramework")
$ErrorActionPreference='Stop'
$revision='d1461375aee4ec3f313170f8eaad12064eb542d9'
if (Test-Path -LiteralPath $Destination) {
    $actual = git -C $Destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $revision) { throw "Existing REFramework checkout is not pinned $revision; preserve it and choose another Destination." }
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'include/reframework/API.h')) -or
        !(Test-Path -LiteralPath (Join-Path $Destination 'dependencies/lua/src/lua.h'))) { throw 'Pinned dependency files missing.' }
    $changes=git -C $Destination status --porcelain -- include/reframework/API.h dependencies/lua
    if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Pinned SDK/Lua sources have local changes or cannot be verified.' }
    return
}
git clone https://github.com/praydog/REFramework.git $Destination
if ($LASTEXITCODE -ne 0) { throw 'REFramework clone failed' }
git -C $Destination checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw 'REFramework checkout failed' }
