param(
    [string]$Port = "COM3",
    [ValidateRange(9600, 10000000)]
    [int]$BaudRate = 115200,
    [ValidateRange(15, 500)]
    [int]$FrameDelayMilliseconds = 32,
    [switch]$NoDazzle
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-ForgixCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [int]$TimeoutMilliseconds = 2000,
        [switch]$IgnoreInteractiveNoise
    )

    $Serial.DiscardInBuffer()
    $Serial.Write("$Command`n")
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Serial.ReadLine().Trim()
            if ($line.Length -gt 0) {
                if ($IgnoreInteractiveNoise -and (
                        $line -eq $Command -or
                        $line -eq "forgix> $Command" -or
                        $line -eq "forgix>" -or
                        $line -match '^id=[0-9A-F]{2} status=[0-9A-F]{2} button=[0-9A-F]{2} count=[0-9]+ fpga_status=[01]$' -or
                        $line -match '^status unavailable:')) {
                    continue
                }
                return $line
            }
        }
        catch [System.TimeoutException] {
            # Keep polling until the overall command deadline expires.
        }
    }
    throw "Timed out waiting for Forgix response to '$Command'."
}

function Assert-Response {
    param(
        [string]$Actual,
        [string]$Pattern,
        [string]$Description
    )

    if ($Actual -notmatch $Pattern) {
        throw "$Description failed. Received: $Actual"
    }
}

function Set-ForgixColor {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [ValidateRange(0, 255)][int]$Red,
        [ValidateRange(0, 255)][int]$Green,
        [ValidateRange(0, 255)][int]$Blue,
        [ValidateRange(0, 255)][int]$Brightness
    )

    $response = Invoke-ForgixCommand $Serial "color $Red $Green $Blue $Brightness"
    if ($response -ne "ok") {
        throw "LED update failed. Received: $response"
    }
}

function Show-Heartbeat {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Name,
        [int]$Red,
        [int]$Green,
        [int]$Blue,
        [int]$DelayMilliseconds
    )

    Write-Host "    $Name heartbeat" -ForegroundColor DarkCyan
    $envelope = @(
        0, 6, 15, 30, 52, 82, 118, 154, 182, 198, 178, 135, 88, 48, 20, 7,
        12, 28, 58, 98, 145, 185, 205, 175, 124, 72, 34, 14, 4, 0
    )
    foreach ($level in $envelope) {
        Set-ForgixColor $Serial $Red $Green $Blue $level
        Start-Sleep -Milliseconds $DelayMilliseconds
    }
    Start-Sleep -Milliseconds 140
}

function Show-ColorWheel {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$DelayMilliseconds
    )

    Write-Host "    spectral color wheel" -ForegroundColor DarkCyan
    $anchors = @(
        @(255, 0, 0),
        @(255, 180, 0),
        @(0, 255, 0),
        @(0, 220, 255),
        @(0, 0, 255),
        @(190, 0, 255),
        @(255, 0, 0)
    )

    for ($segment = 0; $segment -lt $anchors.Count - 1; $segment++) {
        for ($step = 0; $step -lt 12; $step++) {
            $mix = $step / 12.0
            $red = [int][Math]::Round($anchors[$segment][0] * (1.0 - $mix) + $anchors[$segment + 1][0] * $mix)
            $green = [int][Math]::Round($anchors[$segment][1] * (1.0 - $mix) + $anchors[$segment + 1][1] * $mix)
            $blue = [int][Math]::Round($anchors[$segment][2] * (1.0 - $mix) + $anchors[$segment + 1][2] * $mix)
            $brightness = 115 + [int][Math]::Round(35 * [Math]::Sin(($segment * 12 + $step) * [Math]::PI / 18.0))
            Set-ForgixColor $Serial $red $green $blue $brightness
            Start-Sleep -Milliseconds $DelayMilliseconds
        }
    }
}

function Show-Aurora {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$DelayMilliseconds
    )

    Write-Host "    aurora finale" -ForegroundColor DarkCyan
    for ($frame = 0; $frame -lt 54; $frame++) {
        $phase = $frame * [Math]::PI / 27.0
        $red = [int][Math]::Round(38 + 30 * (1.0 + [Math]::Sin($phase * 1.7 + 2.2)))
        $green = [int][Math]::Round(115 + 70 * [Math]::Sin($phase) * [Math]::Sin($phase))
        $blue = [int][Math]::Round(135 + 85 * [Math]::Sin($phase + 1.1) * [Math]::Sin($phase + 1.1))
        $brightness = [int][Math]::Round(105 + 60 * [Math]::Sin($phase * 0.75) * [Math]::Sin($phase * 0.75))
        Set-ForgixColor $Serial $red $green $blue $brightness
        Start-Sleep -Milliseconds ($DelayMilliseconds + 8)
    }

    foreach ($level in @(80, 110, 145, 180, 210, 235, 210, 175, 135, 95, 64)) {
        Set-ForgixColor $Serial 0 255 255 $level
        Start-Sleep -Milliseconds ($DelayMilliseconds + 12)
    }
}

$serial = $null
$resetNeeded = $false
try {
    Write-Step "Opening $Port at $BaudRate baud"
    $serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, None, 8, One
    $serial.Encoding = [System.Text.Encoding]::ASCII
    $serial.NewLine = "`n"
    $serial.ReadTimeout = 100
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $true
    $serial.RtsEnable = $false
    $serial.Open()
    $resetNeeded = $true
    Start-Sleep -Milliseconds 250

    # Two bytes that reach the shell from wherever the board happens to be. The
    # firmware boots into a banner, any key opens the menu, and 'c' opens the
    # shell -- but this script cannot know whether a previous run already moved
    # it on, and the board is not reset by opening the port. So: CR dismisses the
    # banner, aborts a running test, or is an ignored empty line; 'c' then opens
    # the shell from the menu, or is a harmless invalid command if the shell is
    # already up. Sending "quiet" first would not survive this -- the 'q' would
    # be eaten as the banner-dismissing keypress and "uiet" read as menu keys.
    Write-Step "Reaching the command shell from whatever state the board is in"
    $serial.Write("`r")
    Start-Sleep -Milliseconds 250
    $serial.Write("c")
    Start-Sleep -Milliseconds 250
    $serial.DiscardInBuffer()

    Write-Step "Selecting quiet protocol mode"
    $quiet = Invoke-ForgixCommand $serial "quiet" -IgnoreInteractiveNoise
    Assert-Response $quiet '^ok$' "Quiet mode"

    Write-Step "Checking command shell and FPGA identity"
    $help = Invoke-ForgixCommand $serial "help"
    Assert-Response $help '^hello \| color <r> <g> <b> \[brightness\] \| off \| status \| diag \| menu \| reset \| echo <on\|off> \| watch <seconds\|off> \| quiet \| interactive \| help$' "Command help"

    $initialStatus = Invoke-ForgixCommand $serial "status"
    Assert-Response $initialStatus '^id=B6 status=[0-9A-F]{2} button=[0-9A-F]{2} count=([0-9]+) fpga_status=1$' "FPGA status"
    $null = $initialStatus -match ' count=([0-9]+) '
    $initialCount = [int]$Matches[1]
    Write-Host "    $initialStatus" -ForegroundColor Green

    $hello = Invoke-ForgixCommand $serial "hello"
    Assert-Response $hello '^Hello from RP2354 -> FPGA B6$' "Hello readback"
    Write-Host "    $hello" -ForegroundColor Green

    if (-not $NoDazzle) {
        Write-Step "Running RGB heartbeat sequence (press SW1 during the show)"
        Show-Heartbeat $serial "red" 255 0 0 $FrameDelayMilliseconds
        Show-Heartbeat $serial "green" 0 255 0 $FrameDelayMilliseconds
        Show-Heartbeat $serial "blue" 0 0 255 $FrameDelayMilliseconds

        Write-Step "Running color effects"
        Show-ColorWheel $serial $FrameDelayMilliseconds
        Show-Aurora $serial $FrameDelayMilliseconds
    }

    $finalStatus = Invoke-ForgixCommand $serial "status"
    Assert-Response $finalStatus '^id=B6 status=[0-9A-F]{2} button=[0-9A-F]{2} count=([0-9]+) fpga_status=1$' "Final FPGA status"
    $null = $finalStatus -match ' count=([0-9]+) '
    $finalCount = [int]$Matches[1]
    Write-Host "    $finalStatus" -ForegroundColor Green
    if ($finalCount -gt $initialCount) {
        Write-Host "    SW1 observed: press count increased from $initialCount to $finalCount" -ForegroundColor Green
    }
    else {
        Write-Host "    SW1 was not pressed during this run (LED and communication checks still passed)." -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "Forgix hardware smoke test passed." -ForegroundColor Green
}
catch {
    $message = $_.Exception.Message
    if ($message -match "Access to the port '.+' is denied") {
        $message += " Close any serial terminal using $Port. If none is open, reconnect USB to reset the Windows serial device, then retry."
    }
    Write-Host "Forgix hardware smoke test failed: $message" -ForegroundColor Red
    exit 1
}
finally {
    if ($null -ne $serial) {
        if ($serial.IsOpen -and $resetNeeded) {
            try {
                $serial.DiscardInBuffer()
                $serial.Write("reset`n")
                Start-Sleep -Milliseconds 100
            }
            catch {
                Write-Host "Warning: could not restore the default LED state." -ForegroundColor Yellow
            }
        }
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }
}
