# ============================================================================
# mining_node.ps1 - Bucks Primary Mining Node Launcher (Watchdog)
# Run this script to start continuous mining. It restarts on crash.
# For automatic startup, use setup_mining_node.ps1 first.
# ============================================================================

$ScriptDir = $PSScriptRoot
$BuildDir = Join-Path $ScriptDir "build"
$Executable = Join-Path $BuildDir "bucks.exe"
$LogFile = Join-Path $ScriptDir "mining.log"
$WebRoot = Join-Path $ScriptDir "web"

# ---- Tool paths ----
$env:Path = "c:\Users\shafe\OneDrive\Desktop\Blockchain\tools\mingw64\bin;C:\Program Files\OpenSSL-Win64\bin;$env:Path"

# ---- Logging helper ----
function Write-Log {
    param([string]$Message, [string]$Color = "White")
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $line = "[$ts] $Message"
    Write-Host $line -ForegroundColor $Color
    Add-Content -Path $LogFile -Value $line
}

# ---- Power / sleep prevention ----
function Set-NeverSleep {
    Write-Log "Setting power plan to High Performance (no sleep)..." "Cyan"
    powercfg /setactive SCHEME_MIN 2>$null
    # Disable standby, hibernate, monitor timeout on AC
    powercfg /change standby-timeout-ac   0 2>$null
    powercfg /change hibernate-timeout-ac 0 2>$null
    powercfg /change monitor-timeout-ac   0 2>$null
    powercfg /change disk-timeout-ac      0 2>$null
    Write-Log "Sleep/hibernate disabled." "Green"
}

# ---- Verify executable ----
if (!(Test-Path $Executable)) {
    Write-Log "ERROR: bucks.exe not found at $Executable" "Red"
    Write-Log "Please build the project first using CMake." "Yellow"
    exit 1
}

# ---- Apply power settings ----
Set-NeverSleep

# ---- Banner ----
Write-Log "======================================================" "Cyan"
Write-Log "   BUCKS PRIMARY MINING NODE - WATCHDOG LAUNCHER     " "Cyan"
Write-Log "======================================================" "Cyan"
Write-Log "Executable : $Executable" "White"
Write-Log "Log file   : $LogFile"    "White"
Write-Log "Web UI     : http://localhost:8080" "White"
Write-Log "Mining API : http://localhost:8080/api/mining/status" "White"
Write-Log "Wallet API : http://localhost:8080/api/wallets/primary" "White"
Write-Log "------------------------------------------------------" "Cyan"

$restartCount = 0

# ---- Watchdog loop ----
while ($true) {
    $restartCount++
    Write-Log "Starting bucks node (attempt #$restartCount)..." "Yellow"

    Push-Location $BuildDir
    try {
        # Launch with --auto-mine so mining begins immediately on startup
        $proc = Start-Process -FilePath $Executable `
            -ArgumentList "--server", "--auto-mine", "--web-root", $WebRoot `
            -NoNewWindow -PassThru -Wait
        $exitCode = $proc.ExitCode
    }
    catch {
        $exitCode = -1
        Write-Log "Exception launching process: $_" "Red"
    }
    finally {
        Pop-Location
    }

    Write-Log "Node exited with code $exitCode." "Red"

    if ($exitCode -eq 0) {
        Write-Log "Clean shutdown detected. Stopping watchdog." "Green"
        break
    }

    Write-Log "Crash detected. Restarting in 3 seconds..." "Yellow"
    Start-Sleep -Seconds 3
}

Write-Log "Mining node watchdog stopped." "Cyan"
