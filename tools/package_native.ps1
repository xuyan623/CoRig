param(
    [string]$QtPrefix = 'D:\Software\DevTools\Qt\6.12.0\msvc2022_64',
    [switch]$ArchiveOnly,
    # Package exactly the build tree and publish directory named by the caller.
    # Defaults keep the historical developer workflow unchanged.
    [string]$BuildPath = 'build\native',
    [string]$OutputRoot = 'build\native-release',
    [string]$PublishDir = 'dist',
    [string]$VcRedistDir = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildPath)) { $BuildPath } else { Join-Path $repoRoot $BuildPath }
$outputRoot = if ([IO.Path]::IsPathRooted($OutputRoot)) { $OutputRoot } else { Join-Path $repoRoot $OutputRoot }
$publishRoot = if ([IO.Path]::IsPathRooted($PublishDir)) { $PublishDir } else { Join-Path $repoRoot $PublishDir }
$releaseRoot = Join-Path $outputRoot 'CoRig'
$sourceStage = Join-Path $outputRoot 'source'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (!$ArchiveOnly) {
    # Rebuild only this script's generated staging folder. Validate the exact
    # resolved target before recursive removal; never touch projects or data.
    if (Test-Path -LiteralPath $releaseRoot) {
        $resolvedTarget = (Resolve-Path -LiteralPath $releaseRoot).Path
        if ($resolvedTarget -ne [IO.Path]::GetFullPath($releaseRoot) -or !(Split-Path -Parent $resolvedTarget).Equals([IO.Path]::GetFullPath($outputRoot),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected release staging path' }
        Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
    }
    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
    foreach ($program in @('CoRig.exe','CoRig-cli.exe','CoRig-uninstall.exe','CoRigSdk.dll','CoRig-publish.exe','CoRig-launcher.exe')) {
        Copy-Item -LiteralPath (Join-Path $buildRoot $program) -Destination $releaseRoot
    }
    # Qt's dependency scanner includes the actual QML imports and the C++
    # runtime. The application and official plugins carry no Python runtime.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & (Join-Path $QtPrefix 'bin\windeployqt.exe') --release --qmldir (Join-Path $repoRoot 'src\qml') --no-translations --no-opengl-sw (Join-Path $releaseRoot 'CoRig.exe') 2>&1 | Out-Null
    $deployExit = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($deployExit -ne 0) { throw 'Qt deployment failed' }
    # The standalone uninstaller uses Widgets, which the Quick application does
    # not depend on. Scan it too so uninstall works without a developer Qt PATH.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & (Join-Path $QtPrefix 'bin\windeployqt.exe') --release --no-translations --no-opengl-sw (Join-Path $releaseRoot 'CoRig-uninstall.exe') 2>&1 | Out-Null
    $deployExit = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($deployExit -ne 0) { throw 'Uninstaller Qt deployment failed' }
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & (Join-Path $QtPrefix 'bin\windeployqt.exe') --release --dir $releaseRoot --no-translations --no-opengl-sw (Join-Path $buildRoot 'corig_official.dll') 2>&1 | Out-Null
    $deployExit = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($deployExit -ne 0) { throw 'Official plugin Qt deployment failed' }
    New-Item -ItemType Directory -Path (Join-Path $releaseRoot 'plugins') -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $buildRoot 'plugins') -Directory | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $releaseRoot 'plugins') -Recurse -Force }
    # The publisher resolves python/collect.py next to CoRig-publish.exe; ship
    # the collector scripts with the application instead of relying on a source
    # checkout on the publishing machine.
    Copy-Item -LiteralPath (Join-Path $repoRoot 'python') -Destination $releaseRoot -Recurse -Force
    # The GUI export command runs windeployqt and needs the QML sources; both
    # ship with the application so recipients never select a Qt installation.
    Copy-Item -LiteralPath (Join-Path $repoRoot 'src\qml') -Destination (Join-Path $releaseRoot 'publish-qml') -Recurse -Force
    foreach ($tool in @('windeployqt.exe','qmlimportscanner.exe')) {
        Copy-Item -LiteralPath (Join-Path $QtPrefix "bin\$tool") -Destination $releaseRoot -Force
    }
    if (!$VcRedistDir) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsInstall) {
                $redistRoot = Join-Path $vsInstall 'VC\Redist\MSVC'
                if (Test-Path -LiteralPath $redistRoot) {
                    # MSVC\v143 is a compatibility alias without app-local CRT
                    # files; select only version directories that actually carry
                    # the x64 CRT payload the release requires.
                    $VcRedistDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
                        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'x64\Microsoft.VC143.CRT') } |
                        Sort-Object Name | Select-Object -Last 1 -ExpandProperty FullName
                }
            }
        }
    }
    if (!$VcRedistDir -or !(Test-Path -LiteralPath (Join-Path $VcRedistDir 'x64\Microsoft.VC143.CRT'))) { throw '找不到 VC++ x64 运行库，请通过 -VcRedistDir 指定 Redist\MSVC 目录' }
    New-Item -ItemType Directory -Path (Join-Path $releaseRoot 'tools\vc\x64') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $VcRedistDir 'x64\Microsoft.VC143.CRT') -Destination (Join-Path $releaseRoot 'tools\vc\x64') -Recurse -Force
    # Qt 6.12's QRhi loads these D3D shader compilers dynamically, so they are
    # not import-table dependencies. Keep one coherent x64 set in the installed
    # application; the GUI publisher copies it from here without requiring the
    # recipient or the publisher to have a Windows SDK on PATH.
    $d3dRoot = Join-Path $releaseRoot 'd3d\x64'
    New-Item -ItemType Directory -Path $d3dRoot -Force | Out-Null
    $sdkD3d = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist\D3D\x64'
    if ($env:WindowsSdkDir) {
        $candidate = Join-Path $env:WindowsSdkDir 'Redist\D3D\x64'
        if (Test-Path -LiteralPath $candidate) { $sdkD3d = $candidate }
    }
    foreach ($name in @('d3dcompiler_47.dll','dxcompiler.dll','dxil.dll')) {
        $source = Join-Path $sdkD3d $name
        if (!(Test-Path -LiteralPath $source)) { throw "找不到 $name；请安装 Windows SDK 10，或在 WindowsSdkDir 指向的 SDK 中补齐 Redist/D3D/x64" }
        Copy-Item -LiteralPath $source -Destination $d3dRoot -Force
    }
    Set-Content -LiteralPath (Join-Path $releaseRoot 'portable.ini') -Value "[CoRig]`nportable=true" -Encoding ascii
}
if (!(Test-Path -LiteralPath (Join-Path $releaseRoot 'CoRig.exe'))) { throw 'Build and deploy before archiving' }
$references = Join-Path $repoRoot 'skills\corig\references'
foreach ($name in @('API.md','PLUGIN_SDK.md','COMMAND_SCHEMA.json')) { Copy-Item -LiteralPath (Join-Path $repoRoot "docs\$name") -Destination $references -Force }
foreach ($name in @('docs','skills','licenses')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $releaseRoot -Recurse -Force
}
foreach ($name in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md')) { Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $releaseRoot -Force }
New-Item -ItemType Directory -Path (Join-Path $releaseRoot 'examples'),(Join-Path $releaseRoot 'project'),(Join-Path $releaseRoot 'verification') -Force | Out-Null
foreach ($name in @('SerialPlot.crpj','FunctionLab.crpj','RecordPlayback.crpj')) { Copy-Item -LiteralPath (Join-Path $repoRoot "examples\$name") -Destination (Join-Path $releaseRoot 'examples') -Force }
# Ship evidence produced by the CoRig build, keeping historical runs local.
$verificationRoot = Join-Path $repoRoot 'verification\corig-packaged'
$releaseReport = Join-Path $verificationRoot 'report.json'
if (Test-Path -LiteralPath $releaseReport) {
    Copy-Item -LiteralPath $releaseReport -Destination (Join-Path $releaseRoot 'verification\release.json') -Force
}
foreach ($name in @('interface','workbench','instruments','animation','recording','ai')) {
    $report = Join-Path $verificationRoot "$name\report.json"
    if ($name -eq 'interface') { $report = Join-Path $verificationRoot 'interface\result.json' }
    if (Test-Path -LiteralPath $report) {
        Copy-Item -LiteralPath $report -Destination (Join-Path $releaseRoot "verification\$name.json") -Force
    }
}
foreach ($name in @('native','documentation','plugins','installer')) {
    $report = Join-Path $repoRoot "verification\corig-$name\report.json"
    if (Test-Path -LiteralPath $report) {
        Copy-Item -LiteralPath $report -Destination (Join-Path $releaseRoot "verification\$name.json") -Force
    }
}
$pythonFiles = Get-ChildItem -LiteralPath $releaseRoot -Recurse -File | Where-Object { $_.Name -match '^python\d*\.exe$|^python\d*\.dll$|^PySide|^pyqtgraph' -or $_.FullName -match '[\\/]_internal[\\/]' }
if ($pythonFiles) { throw 'The release unexpectedly contains an old Python runtime' }
$publishDirectory = $publishRoot
# Release validation now writes internal caches beside the executable even with
# an external data-dir. Remove only generated scratch entries before archiving.
foreach ($name in @('cache','.sessions','native.log')) {
    $scratch = Join-Path $releaseRoot $name
    if (Test-Path -LiteralPath $scratch) {
        $resolved = (Resolve-Path -LiteralPath $scratch).Path
        if ([IO.Path]::GetDirectoryName($resolved) -ne [IO.Path]::GetFullPath($releaseRoot) -or
            ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Unexpected release scratch path'
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
# Record individual shipped files rather than assuming ownership of whole Qt,
# documentation or SDK directories during uninstall. Runtime data is excluded.
$manifestFiles = @(Get-ChildItem -LiteralPath $releaseRoot -Recurse -File | Where-Object {
    $relative = $_.FullName.Substring($releaseRoot.Length + 1).Replace('\','/')
    $relative -notmatch '^(project|cache|\.sessions)/' -and $relative -notin @('settings.ini','native.log','install-manifest.json')
} | ForEach-Object { $_.FullName.Substring($releaseRoot.Length + 1).Replace('\','/') } | Sort-Object)
$manifestDirectories = @(Get-ChildItem -LiteralPath $releaseRoot -Recurse -Directory | Where-Object {
    $_.FullName.Substring($releaseRoot.Length + 1).Replace('\','/') -notmatch '^(project|cache|\.sessions)(/|$)'
} | ForEach-Object { $_.FullName.Substring($releaseRoot.Length + 1).Replace('\','/') } | Sort-Object)
@{application='CoRig';version=1;files=$manifestFiles;directories=$manifestDirectories} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $releaseRoot 'install-manifest.json') -Encoding utf8
New-Item -ItemType Directory -Path $publishDirectory -Force | Out-Null
$binaryArchive = Join-Path $publishDirectory 'CoRig-Windows-x64.zip'
Compress-Archive -LiteralPath $releaseRoot -DestinationPath $binaryArchive -CompressionLevel Optimal -Force
$sdkArchive = Join-Path $publishDirectory 'CoRig-SDK-Windows-x64.zip'
Compress-Archive -LiteralPath (Join-Path $buildRoot 'sdk\CoRigSdk') -DestinationPath $sdkArchive -CompressionLevel Optimal -Force

# Corresponding source contains the native modules and vendored libssh2, but
# never local settings, private projects, virtual environments or API tokens.
if (Test-Path -LiteralPath $sourceStage) {
    $resolvedTarget = (Resolve-Path -LiteralPath $sourceStage).Path
    if ($resolvedTarget -ne [IO.Path]::GetFullPath($sourceStage) -or !(Split-Path -Parent $resolvedTarget).Equals([IO.Path]::GetFullPath($outputRoot),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected source staging path' }
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
$sourceRoot = Join-Path $sourceStage 'CoRig'
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
foreach ($name in @('src','python','third_party','tests','tools','docs','skills','sdk','installer','licenses','CMakeLists.txt','build.ps1','CONTEXT.md','README.md','LICENSE','THIRD_PARTY_NOTICES.md','.gitignore')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $sourceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'examples') -Force | Out-Null
foreach ($name in @('SerialPlot.crpj','FunctionLab.crpj','RecordPlayback.crpj')) { Copy-Item -LiteralPath (Join-Path $repoRoot "examples\$name") -Destination (Join-Path $sourceRoot 'examples') }
Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object { $_.Extension -eq '.pyc' } | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
$sourceArchive = Join-Path $publishDirectory 'CoRig-source.zip'
Compress-Archive -LiteralPath $sourceRoot -DestinationPath $sourceArchive -CompressionLevel Optimal -Force
Get-Item -LiteralPath $binaryArchive,$sdkArchive,$sourceArchive | Select-Object FullName,Length
