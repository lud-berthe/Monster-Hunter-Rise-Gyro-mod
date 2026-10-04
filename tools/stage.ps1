param([string]$BuildDirectory = "$PSScriptRoot/../build", [string]$Configuration='Release')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$BuildDirectory=[IO.Path]::GetFullPath($BuildDirectory)
$manifest=Join-Path $BuildDirectory "mhr-stage-$Configuration.txt"
if (!(Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Missing $manifest. Configure and build the mod against an installed shared GyroLib SDK first (GYROLIB_SDK_DIR)."
}
$inputs=@(Get-Content -LiteralPath $manifest)
if ($inputs.Count -ne 4) { throw "Invalid staging manifest: $manifest. Reconfigure the build." }
$plugin=$inputs[0]
$sdkRuntime=$inputs[1]
$sdkNotices=$inputs[2]
$reframeworkSource=$inputs[3]
$runtime=Join-Path (Split-Path -Parent $plugin) 'gyrolib.dll'
$required=@($plugin, $sdkRuntime, $runtime,
    (Join-Path $projectRoot 'LICENSE'),
    (Join-Path $reframeworkSource 'LICENSE'),
    (Join-Path $reframeworkSource 'dependencies/lua/src/lua.h'),
    (Join-Path $sdkNotices 'licenses/LICENSE'))
foreach ($path in $required) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package input: $path" }
}
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'reframework/autorun') -PathType Container)) {
    throw 'Missing Lua source directory.'
}
if ((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $sdkRuntime -Algorithm SHA256).Hash) {
    throw 'The build runtime differs from the configured SDK. Rebuild before staging so the runtime and notices match.'
}
$lua=Get-Content -LiteralPath (Join-Path $reframeworkSource 'dependencies/lua/src/lua.h') -Raw
$start=$lua.LastIndexOf('/******************************************************************************')
if ($start -lt 0) { throw 'Lua license block not found in the pinned lua.h.' }

# Recreate only this generated package so obsolete DLLs and nested copies cannot
# survive repeated staging. Refuse redirected directories before any deletion.
$dist=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$stage=[IO.Path]::GetFullPath((Join-Path $dist 'MHRGyro'))
if ($stage -ne (Join-Path $projectRoot 'dist\MHRGyro') -or
        !(($stage + [IO.Path]::DirectorySeparatorChar).StartsWith(
            $dist + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) {
    throw "Unsafe staging path: $stage"
}
foreach ($path in @($dist, $stage)) {
    if ((Test-Path -LiteralPath $path) -and
        ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing redirected staging directory: $path"
    }
}
if (Test-Path -LiteralPath $stage) {
    if (Get-ChildItem -LiteralPath $stage -Recurse -Force | Where-Object {
        $_.Attributes -band [IO.FileAttributes]::ReparsePoint
    }) { throw "Refusing to clean a package containing redirected paths: $stage" }
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force (Join-Path $stage 'reframework/plugins'),
    (Join-Path $stage 'licenses') | Out-Null
Copy-Item -LiteralPath $plugin -Destination (Join-Path $stage 'reframework/plugins/MHRGyro.dll')
Copy-Item -LiteralPath (Join-Path $projectRoot 'reframework/autorun') -Destination (Join-Path $stage 'reframework') -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $stage 'licenses/MHRGyro.txt')
Copy-Item -LiteralPath (Join-Path $reframeworkSource 'LICENSE') -Destination (Join-Path $stage 'licenses/REFramework.txt')
$end=$lua.IndexOf('*/', $start)
if ($end -lt 0) { throw 'Lua license terminator not found.' }
$lua.Substring($start, $end+2-$start) | Set-Content -LiteralPath (Join-Path $stage 'licenses/Lua.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $sdkNotices 'licenses/LICENSE') -Destination (Join-Path $stage 'licenses/GyroLib.txt')
Write-Output "Staged mod-only package: $stage. No game files were modified."
