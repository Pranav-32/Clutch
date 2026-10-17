<#
.SYNOPSIS
Checks that a copied Clutch executable starts without compiler runtime DLLs.
.DESCRIPTION
Copies only clutch.exe into a fresh directory and launches it with a PATH that
contains Windows system directories only. No-argument, help and version checks
must print output and exit successfully. Fixtures remain available for inspection.
.PARAMETER Clutch
Absolute or resolvable path to the executable under test.
.PARAMETER WorkDirectory
Parent directory for the isolated, uniquely named executable copy.
#>
param(
    [Parameter(Mandatory=$true)][string]$Clutch,
    [Parameter(Mandatory=$true)][string]$WorkDirectory
)

$ErrorActionPreference = 'Stop'
$Clutch = (Resolve-Path -LiteralPath $Clutch).Path
$fixture = Join-Path $WorkDirectory ([guid]::NewGuid().ToString())
New-Item -ItemType Directory -Force $fixture | Out-Null
$executable = Join-Path $fixture 'clutch.exe'
Copy-Item -LiteralPath $Clutch -Destination $executable
# Bound startup checks so a stuck executable cannot hang the test runner.
$startupTimeoutMilliseconds = 10000

# Direct process creation lets each child have an isolated DLL search PATH
# without changing the environment of the test runner or any later build.
foreach ($arguments in @('', '--help', '--version')) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $executable
    $start.Arguments = $arguments
    $start.WorkingDirectory = $fixture
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Could not start the copied Clutch executable' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($startupTimeoutMilliseconds)) {
            $process.Kill()
            throw "Copied Clutch timed out: $arguments"
        }

        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0 -or $stdout -notmatch '(?i)clutch') {
            throw "Copied Clutch failed: '$arguments', exit $($process.ExitCode), stdout: $stdout, stderr: $stderr"
        }
    } finally {
        $process.Dispose()
    }
}

Write-Output 'PASS: copied Clutch starts without compiler directories on PATH'
