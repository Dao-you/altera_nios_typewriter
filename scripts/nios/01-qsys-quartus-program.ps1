[CmdletBinding()]
param(
    [string]$NiosShell,
    [string]$QsysFile = "nios.qsys",
    [string]$OutputDirectory = "nios",
    [string]$Project = "EP4",
    [string]$SofFile = "output_files/EP4.sof",
    [switch]$SkipProgram
)

. (Join-Path $PSScriptRoot "_common.ps1")

$repoRoot = Get-ProjectRoot
$repoRootBash = Quote-BashArg (ConvertTo-CygwinPath $repoRoot)
$qsysFileBash = Quote-BashArg ($QsysFile -replace '\\', '/')
$outputDirectoryBash = Quote-BashArg ($OutputDirectory -replace '\\', '/')
$projectBash = Quote-BashArg $Project

$commands = @(
    "cd $repoRootBash",
    "qsys-generate $qsysFileBash --synthesis=VERILOG --output-directory=$outputDirectoryBash",
    "quartus_sh --flow compile $projectBash"
)

if (-not $SkipProgram) {
    $programOption = "p;" + ($SofFile -replace '\\', '/')
    $commands += "quartus_pgm -m jtag -o $(Quote-BashArg $programOption)"
}

Invoke-NiosBash -Command ($commands -join " && ") -NiosShell $NiosShell
