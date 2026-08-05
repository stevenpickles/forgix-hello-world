<#
.SYNOPSIS
    Long-duration USB CDC soak harness for the Forgix lockup investigation.

.DESCRIPTION
    Opens the serial port exactly once and holds it open for the whole run. It
    never toggles control lines and never reopens the port after a failure,
    because a reopen destroys the evidence that distinguishes a wedged device
    from a recoverable host session.

    Every received line is written to a timestamped log. Gaps between receptions
    are measured against the firmware's own output cadence, so a stalled device
    is detected without polling it. Optional numbered pings exercise the
    host-to-device direction.

    On a failure it records the last reception, the last sequence number, and
    the largest gap, then prints the Stage 4 capture checklist from
    docs/usb-cdc-debugging.md and exits without touching the board.

    A `diag:` boot report arriving on the still-open port means the watchdog
    reset the board; it is captured automatically and flagged in the log.

.PARAMETER ValidateOnly
    Check the parameters and the log directory, then exit without opening the
    port. Used to verify the harness on a machine with no board attached.

.EXAMPLE
    ./scripts/soak_serial.ps1 -Port COM3 -DurationMinutes 20

.EXAMPLE
    ./scripts/soak_serial.ps1 -Port COM3 -DurationMinutes 120 -SendIntervalSeconds 10
#>
[CmdletBinding()]
param(
    [string]$Port = "COM3",
    [int]$BaudRate = 115200,
    [bool]$Dtr = $true,
    [bool]$Rts = $false,
    # 0 runs until the device fails or the operator presses Ctrl-C.
    [int]$DurationMinutes = 0,
    [int]$GapWarnSeconds = 5,
    [int]$GapFailSeconds = 30,
    # 0 disables pings, leaving the session passive.
    [int]$SendIntervalSeconds = 0,
    [string]$PingCommand = "status",
    [string]$LogDirectory = "build/soak-logs",
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-Stamp {
    return (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff")
}

# ---------------------------------------------------------------- validation
if ($BaudRate -le 0) { throw "BaudRate must be positive: $BaudRate" }
if ($DurationMinutes -lt 0) { throw "DurationMinutes cannot be negative: $DurationMinutes" }
if ($GapWarnSeconds -le 0) { throw "GapWarnSeconds must be positive: $GapWarnSeconds" }
if ($GapFailSeconds -le $GapWarnSeconds) {
    throw "GapFailSeconds ($GapFailSeconds) must exceed GapWarnSeconds ($GapWarnSeconds)"
}
if ($SendIntervalSeconds -lt 0) { throw "SendIntervalSeconds cannot be negative: $SendIntervalSeconds" }
if ($SendIntervalSeconds -gt 0 -and [string]::IsNullOrWhiteSpace($PingCommand)) {
    throw "PingCommand is required when SendIntervalSeconds is greater than zero"
}
if ($Port -notmatch '^COM[0-9]+$') {
    throw "Port must name a Windows serial port, for example COM3: $Port"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($LogDirectory)) {
    $LogDirectory = Join-Path $repoRoot $LogDirectory
}
if (-not (Test-Path $LogDirectory)) {
    $null = New-Item -ItemType Directory -Force -Path $LogDirectory
}

$logPath = Join-Path $LogDirectory ("soak-{0}-{1}.log" -f $Port, (Get-Date).ToString("yyyyMMdd-HHmmss"))

$plan = [ordered]@{
    Port                = $Port
    BaudRate            = $BaudRate
    Dtr                 = $Dtr
    Rts                 = $Rts
    DurationMinutes     = if ($DurationMinutes -eq 0) { "until failure or Ctrl-C" } else { $DurationMinutes }
    GapWarnSeconds      = $GapWarnSeconds
    GapFailSeconds      = $GapFailSeconds
    SendIntervalSeconds = if ($SendIntervalSeconds -eq 0) { "passive (no pings)" } else { $SendIntervalSeconds }
    PingCommand         = $PingCommand
    LogFile             = $logPath
}

Write-Step "Soak configuration"
$plan.GetEnumerator() | ForEach-Object { Write-Host ("    {0,-20}{1}" -f $_.Key, $_.Value) }

if ($ValidateOnly) {
    Write-Host ""
    Write-Host "Parameters and log directory are valid. No port was opened." -ForegroundColor Green
    exit 0
}

# --------------------------------------------------------------------- setup
$logWriter = [System.IO.StreamWriter]::new($logPath, $false)
$logWriter.AutoFlush = $true

function Write-Log {
    param([string]$Message)
    $line = "{0}  {1}" -f (Get-Stamp), $Message
    $logWriter.WriteLine($line)
}

foreach ($entry in $plan.GetEnumerator()) {
    Write-Log ("# {0}: {1}" -f $entry.Key, $entry.Value)
}

$serial = $null
$startTime = Get-Date
$lastReceive = $startTime
$lastSend = $startTime
$maxGap = [TimeSpan]::Zero
$receivedLines = 0
$sequence = 0
$lastLine = "<none>"
$warnedGap = $false
$failureReason = $null
$sawBootReport = $false
$partial = ""

try {
    Write-Step "Opening $Port at $BaudRate baud (this is the only open of the run)"
    $serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, None, 8, One
    $serial.Encoding = [System.Text.Encoding]::ASCII
    $serial.NewLine = "`n"
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 2000
    $serial.DtrEnable = $Dtr
    $serial.RtsEnable = $Rts
    $serial.Open()
    Write-Log "opened $Port dtr=$Dtr rts=$Rts"
    Write-Host "    Logging to $logPath" -ForegroundColor Green
    Write-Host "    Press Ctrl-C to stop. The port is never reopened automatically." -ForegroundColor DarkGray

    $deadline = if ($DurationMinutes -gt 0) { $startTime.AddMinutes($DurationMinutes) } else { [DateTime]::MaxValue }

    while ((Get-Date) -lt $deadline) {
        # -------------------------------------------------------------- read
        $chunk = ""
        try {
            $chunk = $serial.ReadExisting()
        }
        catch [System.TimeoutException] {
            $chunk = ""
        }
        catch {
            $failureReason = "read exception: $($_.Exception.Message)"
            break
        }

        if ($chunk.Length -gt 0) {
            $partial += $chunk
            while ($partial.Contains("`n")) {
                $split = $partial.IndexOf("`n")
                $line = $partial.Substring(0, $split).TrimEnd("`r")
                $partial = $partial.Substring($split + 1)
                if ($line.Length -eq 0) { continue }

                $now = Get-Date
                $gap = $now - $lastReceive
                if ($gap -gt $maxGap) { $maxGap = $gap }
                $lastReceive = $now
                $receivedLines++
                $lastLine = $line
                $warnedGap = $false

                Write-Log ("RX  {0}" -f $line)

                # A boot report on the still-open port means the board reset
                # under us; the retained marker names where it was stuck.
                if ($line -match '^diag: boot=') {
                    $sawBootReport = $true
                    Write-Log "!!  device reported a boot after the session started: $line"
                    Write-Host "    RESET DETECTED: $line" -ForegroundColor Yellow
                }
            }
        }

        # -------------------------------------------------------------- send
        $now = Get-Date
        if ($SendIntervalSeconds -gt 0 -and ($now - $lastSend).TotalSeconds -ge $SendIntervalSeconds) {
            $sequence++
            try {
                $serial.WriteLine($PingCommand)
                Write-Log ("TX  seq={0} {1}" -f $sequence, $PingCommand)
            }
            catch {
                $failureReason = "write exception at seq=$sequence : $($_.Exception.Message)"
                break
            }
            $lastSend = $now
        }

        # --------------------------------------------------------- gap check
        # The in-progress gap counts toward LargestGap here, not only when a
        # line finally arrives: the stall that trips the failure limit below is
        # the largest gap of the run, and folding it in only on reception would
        # leave it out of its own evidence.
        $idle = ((Get-Date) - $lastReceive)
        if ($idle -gt $maxGap) { $maxGap = $idle }
        if ($idle.TotalSeconds -ge $GapFailSeconds) {
            $failureReason = "no data for {0:N1} s (limit {1} s)" -f $idle.TotalSeconds, $GapFailSeconds
            break
        }
        if ($idle.TotalSeconds -ge $GapWarnSeconds -and -not $warnedGap) {
            $warnedGap = $true
            Write-Log ("WARN gap {0:N1} s with no received line" -f $idle.TotalSeconds)
            Write-Host ("    gap {0:N1} s" -f $idle.TotalSeconds) -ForegroundColor Yellow
        }

        Start-Sleep -Milliseconds 50
    }
}
finally {
    $elapsed = (Get-Date) - $startTime

    Write-Host ""
    Write-Step "Soak summary"
    # LastReceived is the time of the last actual reception, not the time this
    # summary printed -- on a gap failure those differ by the whole failure
    # window, and the operator correlates ETW/USBView captures against this
    # timestamp. <none> mirrors LastLine when nothing ever arrived.
    $summary = [ordered]@{
        Elapsed        = "{0:hh\:mm\:ss}" -f $elapsed
        ReceivedLines  = $receivedLines
        LastReceived   = if ($receivedLines -gt 0) { $lastReceive.ToString("yyyy-MM-dd HH:mm:ss.fff") } else { "<none>" }
        LastLine       = $lastLine
        LastSequence   = $sequence
        LargestGap     = "{0:N1} s" -f $maxGap.TotalSeconds
        BootReportSeen = $sawBootReport
        Result         = if ($failureReason) { "FAILED: $failureReason" } else { "completed" }
    }
    foreach ($entry in $summary.GetEnumerator()) {
        Write-Host ("    {0,-16}{1}" -f $entry.Key, $entry.Value)
        Write-Log ("# {0}: {1}" -f $entry.Key, $entry.Value)
    }

    if ($failureReason) {
        Write-Host ""
        Write-Host "Capture this evidence BEFORE touching the board:" -ForegroundColor Yellow
        @(
            "1. Record the exact time, the LED color, and the last sequence number above.",
            "   green=healthy  red=endpoint wedged, loop alive  magenta=suspended or SOF frozen",
            "   blue=DTR low  white x3=FPGA reconfigured and recovered",
            "2. Check whether Device Manager and USBView still list the device and its CDC interface.",
            "3. Save the Windows USB ETW trace and device events.",
            "4. Close this window, then attempt exactly one clean reopen of $Port.",
            "5. Only then try 'picotool reboot -f -u'; it deliberately changes device state.",
            "6. Power-cycle last, after every observation above is recorded.",
            "",
            "Then read the result against the mode-1 decision tree in",
            "docs/lockup-investigation-plan.md and append a row to the results log in",
            "docs/usb-cdc-debugging.md."
        ) | ForEach-Object { Write-Host "    $_" }
    }

    if ($null -ne $serial) {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
    Write-Host ""
    Write-Host "Log: $logPath" -ForegroundColor Green
    $logWriter.Dispose()
}

if ($failureReason) { exit 1 }
exit 0
