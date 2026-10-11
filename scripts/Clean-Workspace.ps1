[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Resolve against this script, never the caller's current directory.
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceItem = Get-Item -LiteralPath $workspaceRoot -Force
if ($workspaceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
    throw "Refusing to clean a linked workspace: $workspaceRoot"
}

# Preflight every target before deleting anything. Never follow links/junctions.
$targets = foreach ($name in @('build', '.vs', 'out')) {
    $targetPath = [System.IO.Path]::GetFullPath((Join-Path $workspaceRoot $name))
    if ([System.IO.Path]::GetDirectoryName($targetPath) -ne $workspaceRoot) {
        throw "Target is outside the workspace: $targetPath"
    }
    if (Test-Path -LiteralPath $targetPath) {
        $targetItem = Get-Item -LiteralPath $targetPath -Force
        if (-not $targetItem.PSIsContainer -or
            ($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "Expected an ordinary generated directory: $targetPath"
        }
        $pending = [System.Collections.Generic.Stack[string]]::new()
        $pending.Push($targetPath)
        while ($pending.Count -gt 0) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                    throw "Refusing to clean a directory containing a link: $($item.FullName)"
                }
                if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            }
        }
        $targetPath
    }
}

foreach ($targetPath in $targets) {
    if ($PSCmdlet.ShouldProcess($targetPath, 'Remove generated build directory')) {
        Remove-Item -LiteralPath $targetPath -Recurse -Force
        Write-Host "Removed $targetPath"
    }
}
