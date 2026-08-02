<#
    Add or remove the install directory in the CURRENT USER's PATH, for installer/windows/wxnote.nsi.

    Why a PowerShell helper instead of doing it in NSIS directly - two traps, both of which silently
    corrupt a user's PATH rather than failing:

      1. LENGTH. This NSIS is the stock build, NSIS_MAX_STRLEN=1024. `ReadRegStr` on a PATH longer than
         that truncates it, and writing the truncated value back destroys every entry past the cut.
         Real PATHs routinely exceed 1024 characters. The EnVar plugin exists to solve this but is not
         bundled with NSIS and is not installed by the CI runner, so it cannot be relied on.
      2. VALUE TYPE. PATH is normally REG_EXPAND_SZ so entries like %USERPROFILE%\bin work. .NET's
         [Environment]::SetEnvironmentVariable(..., 'User') rewrites it as REG_SZ, after which those
         entries are dead strings. So this touches the registry directly and preserves the value kind.

    Idempotent: adding twice is a no-op, and comparison ignores case and a trailing backslash, so it
    will not append a duplicate that differs only cosmetically. Removing takes out exactly this
    directory and leaves every other entry untouched.

    Exit codes: 0 = done (including "nothing to do"), 1 = failed. The installer treats 1 as a warning,
    never as a failed install - a PATH entry is a convenience, not part of a working wxNote.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Add', 'Remove')][string] $Action,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]     $Directory
)

$ErrorActionPreference = 'Stop'

function Normalize([string] $p) { return $p.Trim().TrimEnd('\') }

try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment', $true)
    if ($null -eq $key) { Write-Output 'cannot open HKCU\Environment'; exit 1 }
    try {
        # DoNotExpandEnvironmentNames: read the literal '%VAR%' text, not its expansion. Without this
        # the write-back would bake today's expansion in permanently.
        $current = [string] $key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $kind = if ($key.GetValueNames() -contains 'Path') { $key.GetValueKind('Path') }
                else { [Microsoft.Win32.RegistryValueKind]::ExpandString }

        $target = Normalize $Directory
        $parts  = @($current -split ';' | Where-Object { $_ -ne '' })

        if ($Action -eq 'Add') {
            if ($parts | Where-Object { (Normalize $_) -ieq $target }) { Write-Output 'already present'; exit 0 }
            $key.SetValue('Path', (($parts + $Directory) -join ';'), $kind)
            Write-Output 'added'
        }
        else {
            $kept = @($parts | Where-Object { (Normalize $_) -ine $target })
            if ($kept.Count -eq $parts.Count) { Write-Output 'not present'; exit 0 }
            $key.SetValue('Path', ($kept -join ';'), $kind)
            Write-Output 'removed'
        }
        exit 0
    }
    finally { $key.Close() }
}
catch {
    Write-Output ("PATH update failed: " + $_.Exception.Message)
    exit 1
}
