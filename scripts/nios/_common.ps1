Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$localEnvPath = Join-Path $PSScriptRoot "local.env.ps1"
if (Test-Path -LiteralPath $localEnvPath) {
    . $localEnvPath
}

function Get-ProjectRoot {
    $dir = (Resolve-Path -LiteralPath $PSScriptRoot).Path

    while ($true) {
        if ((Test-Path -LiteralPath (Join-Path $dir "EP4.qpf")) -and
            (Test-Path -LiteralPath (Join-Path $dir "nios.qsys"))) {
            return $dir
        }

        $parent = Split-Path -Parent $dir
        if ([string]::IsNullOrWhiteSpace($parent) -or ($parent -eq $dir)) {
            throw "Could not locate the project root from script path: $PSScriptRoot"
        }

        $dir = $parent
    }
}

function ConvertTo-CygwinPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath -match '^([A-Za-z]):\\(.*)$') {
        $drive = $matches[1].ToLowerInvariant()
        $rest = $matches[2] -replace '\\', '/'
        return "/cygdrive/$drive/$rest"
    }

    return ($fullPath -replace '\\', '/')
}

function Quote-BashArg {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    if ($Value.Length -eq 0) {
        return "''"
    }

    return ($Value -replace '([^A-Za-z0-9_./:-])', '\$1')
}

function Test-NiosShellPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return ((-not [string]::IsNullOrWhiteSpace($Path)) -and
        (Test-Path -LiteralPath $Path -PathType Leaf))
}

function Find-NiosShell {
    $candidates = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($env:SOPC_KIT_NIOS2)) {
        $candidates.Add((Join-Path $env:SOPC_KIT_NIOS2 "Nios II Command Shell.bat"))
    }

    if (-not [string]::IsNullOrWhiteSpace($env:QUARTUS_ROOTDIR)) {
        $quartusParent = Split-Path -Parent $env:QUARTUS_ROOTDIR
        $candidates.Add((Join-Path $quartusParent "nios2eds\Nios II Command Shell.bat"))
    }

    foreach ($root in @("C:\altera", "C:\intelFPGA_lite", "C:\intelFPGA")) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }

        Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $candidates.Add((Join-Path $_.FullName "nios2eds\Nios II Command Shell.bat"))
        }
    }

    foreach ($candidate in $candidates) {
        if (Test-NiosShellPath -Path $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return ""
}

function Resolve-NiosShell {
    param(
        [string]$NiosShell
    )

    if (-not [string]::IsNullOrWhiteSpace($NiosShell)) {
        if (-not (Test-NiosShellPath -Path $NiosShell)) {
            throw "NiosShell path does not exist: $NiosShell"
        }

        return (Resolve-Path -LiteralPath $NiosShell).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($env:NIOS2_COMMAND_SHELL)) {
        if (-not (Test-NiosShellPath -Path $env:NIOS2_COMMAND_SHELL)) {
            throw "NIOS2_COMMAND_SHELL path does not exist: $env:NIOS2_COMMAND_SHELL"
        }

        return (Resolve-Path -LiteralPath $env:NIOS2_COMMAND_SHELL).Path
    }

    return (Find-NiosShell)
}

function Invoke-NiosBash {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [string]$NiosShell
    )

    $resolvedNiosShell = Resolve-NiosShell -NiosShell $NiosShell
    if ([string]::IsNullOrWhiteSpace($resolvedNiosShell)) {
        if (-not (Get-Command bash -ErrorAction SilentlyContinue)) {
            throw "Could not find Nios II Command Shell. Create scripts/nios/local.env.ps1, set NIOS2_COMMAND_SHELL, or pass -NiosShell."
        }

        & bash -lc $Command
    } else {
        & $resolvedNiosShell bash -lc $Command
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}
