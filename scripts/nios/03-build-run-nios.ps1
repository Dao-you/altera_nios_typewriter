[CmdletBinding()]
param(
    [string]$NiosShell,
    [string]$AppDir = "software/niosapp",
    [string]$ElfFile = "software/niosapp/niosapp.elf",
    [string]$MakeTarget = "all",
    [switch]$NoRun
)

. (Join-Path $PSScriptRoot "_common.ps1")

$repoRoot = Get-ProjectRoot
$repoRootBash = Quote-BashArg (ConvertTo-CygwinPath $repoRoot)
$appDirPath = Join-Path $repoRoot $AppDir
$appDirBash = Quote-BashArg (ConvertTo-CygwinPath $appDirPath)
$makeTargetBash = Quote-BashArg $MakeTarget

$commands = @(
    "cd $appDirBash",
    "make QSYS=0 MAKEABLE_LIBRARY_ROOT_DIRS= $makeTargetBash"
)

if (-not $NoRun) {
    $elfFileBash = Quote-BashArg ($ElfFile -replace '\\', '/')
    $commands += "cd $repoRootBash"
    $commands += "nios2-download -g $elfFileBash"
}

Invoke-NiosBash -Command ($commands -join " && ") -NiosShell $NiosShell
