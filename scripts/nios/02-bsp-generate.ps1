[CmdletBinding()]
param(
    [string]$NiosShell,
    [string]$SettingsFile = "software/niosapp_bsp/settings.bsp",
    [string]$BspDir = "software/niosapp_bsp"
)

. (Join-Path $PSScriptRoot "_common.ps1")

$repoRoot = Get-ProjectRoot
$repoRootBash = Quote-BashArg (ConvertTo-CygwinPath $repoRoot)
$settingsFileBash = Quote-BashArg ($SettingsFile -replace '\\', '/')
$bspDirBash = Quote-BashArg ($BspDir -replace '\\', '/')

$commands = @(
    "cd $repoRootBash",
    "nios2-bsp-generate-files --settings $settingsFileBash --bsp-dir $bspDirBash"
)

Invoke-NiosBash -Command ($commands -join " && ") -NiosShell $NiosShell
