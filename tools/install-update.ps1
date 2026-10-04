param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$BuildDirectory="$PSScriptRoot/../build",
    [string]$Configuration='Release',
    [switch]$ScriptsOnly,
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$gameRoot=[IO.Path]::GetFullPath($GameDirectory)
$buildRoot=[IO.Path]::GetFullPath($BuildDirectory)
$gameExecutable=Join-Path $gameRoot 'MonsterHunterRise.exe'
if (!(Test-Path -LiteralPath $gameExecutable -PathType Leaf)) { throw "Rise executable missing: $gameExecutable" }
if (!$ScriptsOnly -and !$CheckOnly) {
    foreach ($taskProcess in @(Get-Process -Name MonsterHunterRise -ErrorAction SilentlyContinue)) {
        if ($taskProcess.Path -eq $gameExecutable) { throw 'Quit Monster Hunter Rise before updating its loaded mod DLL.' }
    }
}
$manifestPath=Join-Path $gameRoot 'reframework/MHRGyro/install-manifest.json'
$legacyManifest=Join-Path $buildRoot 'installed-diagnostic-files.json'
$installed=@()
if (Test-Path -LiteralPath $manifestPath) {
    $installed=@(Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)
} elseif (Test-Path -LiteralPath $legacyManifest) {
    $installed=@(Get-Content -LiteralPath $legacyManifest -Raw | ConvertFrom-Json)
}
$inputs=@(Get-Content -LiteralPath (Join-Path $buildRoot "mhr-stage-$Configuration.txt"))
if ($inputs.Count -ne 4) { throw 'Invalid build staging manifest. Reconfigure the build.' }
$runtimeHash=(Get-FileHash -LiteralPath $inputs[1] -Algorithm SHA256).Hash
$builtRuntime=Join-Path (Split-Path -Parent $inputs[0]) 'gyrolib.dll'
if ((Get-FileHash -LiteralPath $builtRuntime -Algorithm SHA256).Hash -ne $runtimeHash) {
    throw 'Build runtime differs from the configured SDK. Rebuild before installing.'
}
if ($ScriptsOnly -and (Get-FileHash -LiteralPath (Join-Path $gameRoot 'gyrolib.dll') -Algorithm SHA256).Hash -ne $runtimeHash) {
    throw 'Installed GyroLib runtime differs from the SDK used by this build.'
}
if ($ScriptsOnly -and (Get-FileHash -LiteralPath (Join-Path $gameRoot 'reframework/plugins/MHRGyro.dll')).Hash -ne
    (Get-FileHash -LiteralPath $inputs[0]).Hash) { throw 'Lua-only updates require the matching native plugin. Close Rise and install the full build.' }
$plans=@()
if (!$ScriptsOnly) {
    $plans+=[PSCustomObject]@{Source=$inputs[0];Target=(Join-Path $gameRoot 'reframework/plugins/MHRGyro.dll')}
    # Accept a user-updated runtime only if it is already exactly the SDK DLL.
    # Other tracked files still require their original installation hashes.
    $plans+=[PSCustomObject]@{Source=$inputs[1];Target=(Join-Path $gameRoot 'gyrolib.dll')}
    $plans+=[PSCustomObject]@{Source=(Join-Path $projectRoot 'LICENSE');Target=(Join-Path $gameRoot 'reframework/MHRGyro/licenses/MHRGyro.txt')}
    $plans+=[PSCustomObject]@{Source=(Join-Path $inputs[3] 'LICENSE');Target=(Join-Path $gameRoot 'reframework/MHRGyro/licenses/REFramework.txt')}
    $lua=Get-Content -LiteralPath (Join-Path $inputs[3] 'dependencies/lua/src/lua.h') -Raw
    $start=$lua.LastIndexOf('/******************************************************************************')
    if ($start -lt 0) { throw 'Lua license block not found.' }
    $plans+=[PSCustomObject]@{Source=$null;Content=$lua.Substring($start);Target=(Join-Path $gameRoot 'reframework/MHRGyro/licenses/Lua.txt')}
    $noticesRoot=[IO.Path]::GetFullPath($inputs[2])
    $notices=@(Get-Item -LiteralPath (Join-Path $noticesRoot 'THIRD_PARTY.md'))
    foreach ($directory in @('licenses','sdl-changes')) {
        $notices+=@(Get-ChildItem -LiteralPath (Join-Path $noticesRoot $directory) -Recurse -File)
    }
    foreach ($notice in $notices) {
        $relative=$notice.FullName.Substring($noticesRoot.Length+1)
        $plans+=[PSCustomObject]@{Source=$notice.FullName;Target=(Join-Path $gameRoot "reframework/MHRGyro/licenses/GyroLib/$relative")}
    }
}
$scripts=@(Get-Item -LiteralPath (Join-Path $projectRoot 'reframework/autorun/mhr_gyro.lua'))
$scripts+=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'reframework/autorun/mhr_gyro') -File -Filter '*.lua')
foreach ($script in $scripts) {
    $relative=$script.FullName.Substring($projectRoot.Length+1)
    $plans+=[PSCustomObject]@{Source=$script.FullName;Target=(Join-Path $gameRoot $relative)}
}
# Check every predecessor before copying any file. Only our tracked files can
# be replaced; a newly introduced script must not overwrite an existing file.
foreach ($plan in $plans) {
    if ($plan.Source) {
        if (!(Test-Path -LiteralPath $plan.Source -PathType Leaf)) { throw "Missing update input: $($plan.Source)" }
        $hash=(Get-FileHash -LiteralPath $plan.Source -Algorithm SHA256).Hash
    } else {
        $bytes=[Text.Encoding]::UTF8.GetBytes($plan.Content)
        $sha=[Security.Cryptography.SHA256]::Create()
        try { $hash=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','') } finally { $sha.Dispose() }
    }
    $plan | Add-Member -NotePropertyName Hash -NotePropertyValue $hash
    $previous=@($installed | Where-Object { $_.path -eq $plan.Target })
    if (Test-Path -LiteralPath $plan.Target) {
        $currentHash=(Get-FileHash -LiteralPath $plan.Target -Algorithm SHA256).Hash
        $matchingSdkRuntime=$plan.Target -eq (Join-Path $gameRoot 'gyrolib.dll') -and $currentHash -eq $runtimeHash
        $alreadyMatches=$currentHash -eq $plan.Hash
        if (!$alreadyMatches -and ($previous.Count -ne 1 -or
            ($currentHash -ne $previous[0].sha256 -and !$matchingSdkRuntime))) {
            throw "Installed file changed outside this task: $($plan.Target)"
        }
    } elseif ($previous.Count -ne 0) { throw "Tracked installed file is missing: $($plan.Target)" }
}
if ($CheckOnly) {
    Write-Output "Verified $($plans.Count) update inputs and tracked predecessors; no files modified."
    return
}
foreach ($plan in $plans) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $plan.Target) | Out-Null
    if ($plan.Source) { Copy-Item -LiteralPath $plan.Source -Destination $plan.Target }
    else { [IO.File]::WriteAllBytes($plan.Target,[Text.Encoding]::UTF8.GetBytes($plan.Content)) }
    $hash=$plan.Hash
    if ((Get-FileHash -LiteralPath $plan.Target -Algorithm SHA256).Hash -ne $hash) { throw "Update hash mismatch: $($plan.Target)" }
    $previous=@($installed | Where-Object { $_.path -eq $plan.Target })
    if ($previous.Count -eq 1) { $previous[0].sha256=$hash }
    else { $installed+=[PSCustomObject]@{path=$plan.Target;sha256=$hash} }
}
New-Item -ItemType Directory -Force (Split-Path -Parent $manifestPath) | Out-Null
$installed | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
if ($ScriptsOnly) {
    Write-Output "Updated $($plans.Count) Lua scripts; source/game hashes match. Use REFramework > ScriptRunner > Reset scripts to activate."
} else {
    Write-Output "Updated $($plans.Count) mod files; source/game hashes match. F10 opens the native GyroLib GUI (or use its configured shortcut); F9 records diagnostics."
}
