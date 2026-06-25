[CmdletBinding()]
param(
    [string]$NiosShell,
    [string]$AppDir = "software/niosapp",
    [string]$BspDir = "software/niosapp_bsp",
    [string]$SettingsFile = "software/niosapp_bsp/settings.bsp",
    [string]$ElfFile = "software/niosapp/niosapp.elf",
    [string]$MakeTarget = "app",
    [switch]$SkipBspGenerate,
    [switch]$NoRun
)

. (Join-Path $PSScriptRoot "_common.ps1")

$repoRoot = Get-ProjectRoot
$repoRootBash = Quote-BashArg (ConvertTo-CygwinPath $repoRoot)
$appDirPath = Join-Path $repoRoot $AppDir
$appDirBash = Quote-BashArg (ConvertTo-CygwinPath $appDirPath)
$bspDirBash = Quote-BashArg ($BspDir -replace '\\', '/')
$settingsFileBash = Quote-BashArg ($SettingsFile -replace '\\', '/')
$makeTargetBash = Quote-BashArg $MakeTarget

$commands = @(
    "export CYGWIN=nodosfilewarning",
    "cd $repoRootBash"
)

if (-not $SkipBspGenerate) {
    $commands += "if [ ! -f $bspDirBash/HAL/inc/alt_types.h ] || [ ! -f $bspDirBash/drivers/inc/altera_avalon_pio_regs.h ] || [ $settingsFileBash -nt $bspDirBash/Makefile ] || [ nios.sopcinfo -nt $bspDirBash/Makefile ]; then nios2-bsp-generate-files --settings $settingsFileBash --bsp-dir $bspDirBash; fi"
}

$commands += "make COMSPEC= ComSpec= -C $bspDirBash all"
$commands += "cd $appDirBash"
$commands += "make COMSPEC= ComSpec= QSYS=0 MAKEABLE_LIBRARY_ROOT_DIRS= DISABLE_STACKREPORT=1 $makeTargetBash"

if (-not $NoRun) {
    $elfFileBash = Quote-BashArg ($ElfFile -replace '\\', '/')
    $commands += "cd $repoRootBash"
    $commands += "nios2-download -g $elfFileBash"
}

Invoke-NiosBash -Command ($commands -join " && ") -NiosShell $NiosShell
