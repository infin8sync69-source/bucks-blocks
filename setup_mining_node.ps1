# ============================================================================
# setup_mining_node.ps1 - Bucks Primary Mining Node - One-Time Setup
# Run this script ONCE as Administrator to configure Windows for always-on mining.
# ============================================================================

#Requires -RunAsAdministrator

$ScriptDir = $PSScriptRoot
$LauncherScript = Join-Path $ScriptDir "mining_node.ps1"
$TaskName = "BucksMiningNode"
$WalletsJson = Join-Path $ScriptDir "wallets.json"

Write-Host ""
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "   BUCKS MINING NODE - SYSTEM SETUP                  " -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host ""

# ---- 1. Power Plan – High Performance ----
Write-Host "[1/4] Configuring power plan (High Performance, no sleep)..." -ForegroundColor Yellow
powercfg /setactive SCHEME_MIN

# Disable all sleep/timeout on AC power
powercfg /change standby-timeout-ac   0
powercfg /change hibernate-timeout-ac 0
powercfg /change monitor-timeout-ac   0
powercfg /change disk-timeout-ac      0

# Disable hibernate entirely
powercfg /h off

Write-Host "      Power plan set. Sleep/hibernate disabled." -ForegroundColor Green

# ---- 2. Windows Task Scheduler ----
Write-Host "[2/4] Registering Windows Task Scheduler task '$TaskName'..." -ForegroundColor Yellow

# Remove existing task if present
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

$Action = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument "-WindowStyle Hidden -NonInteractive -ExecutionPolicy Bypass -File `"$LauncherScript`""

# Trigger: At logon + at system startup
$TriggerLogon = New-ScheduledTaskTrigger -AtLogOn
$TriggerBoot = New-ScheduledTaskTrigger -AtStartup

$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Days 3650) `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -StartWhenAvailable

$Principal = New-ScheduledTaskPrincipal `
    -UserId "SYSTEM" `
    -LogonType ServiceAccount `
    -RunLevel Highest

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $Action `
    -Trigger $TriggerLogon, $TriggerBoot `
    -Settings $Settings `
    -Principal $Principal `
    -Description "Bucks Blockchain - Primary Mining Node (always-on)" `
    -Force

Write-Host "      Task '$TaskName' registered. Will run at every startup." -ForegroundColor Green

# ---- 3. Set PowerShell execution policy ----
Write-Host "[3/4] Ensuring PowerShell execution policy allows scripts..." -ForegroundColor Yellow
Set-ExecutionPolicy RemoteSigned -Scope LocalMachine -Force 2>$null
Write-Host "      Execution policy set to RemoteSigned." -ForegroundColor Green

# ---- 4. Show wallet info ----
Write-Host "[4/4] Checking primary wallet..." -ForegroundColor Yellow
if (Test-Path $WalletsJson) {
    try {
        $wallets = Get-Content $WalletsJson | ConvertFrom-Json
        if ($wallets.Count -gt 0) {
            $primary = $wallets[0]
            Write-Host ""
            Write-Host "  PRIMARY WALLET" -ForegroundColor Cyan
            Write-Host "  ─────────────────────────────────────────────" -ForegroundColor DarkGray
            if ($primary.PSObject.Properties["mnemonic"] -and $primary.mnemonic) {
                Write-Host "  Mnemonic  : " -NoNewline -ForegroundColor White
                Write-Host $primary.mnemonic -ForegroundColor Yellow
                Write-Host ""
                Write-Host "  >>> WRITE DOWN YOUR MNEMONIC! This is your wallet recovery phrase. <<<" -ForegroundColor Red
            }
            else {
                Write-Host "  Mnemonic  : (not yet generated - start the server to create wallet)" -ForegroundColor DarkGray
            }
            Write-Host "  ─────────────────────────────────────────────" -ForegroundColor DarkGray
            Write-Host ""
        }
    }
    catch {
        Write-Host "      Could not parse wallets.json" -ForegroundColor DarkGray
    }
}
else {
    Write-Host "      wallets.json not found yet - will be created on first run." -ForegroundColor DarkGray
}

# ---- Done ----
Write-Host ""
Write-Host "======================================================" -ForegroundColor Green
Write-Host "   SETUP COMPLETE                                     " -ForegroundColor Green
Write-Host "======================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  The mining node will now:" -ForegroundColor White
Write-Host "   - Start automatically at every Windows boot/login" -ForegroundColor White
Write-Host "   - Restart itself if it crashes" -ForegroundColor White
Write-Host "   - Never sleep or hibernate" -ForegroundColor White
Write-Host "   - Mine all Bucks rewards to your primary wallet" -ForegroundColor White
Write-Host ""
Write-Host "  Manual start  :  .\mining_node.ps1" -ForegroundColor Cyan
Write-Host "  Web UI        :  http://localhost:8080" -ForegroundColor Cyan
Write-Host "  Mining status :  http://localhost:8080/api/mining/status" -ForegroundColor Cyan
Write-Host "  Primary wallet:  http://localhost:8080/api/wallets/primary" -ForegroundColor Cyan
Write-Host ""

# ---- Start now? ----
$start = Read-Host "Start mining node NOW? (Y/n)"
if ($start -ne "n" -and $start -ne "N") {
    Write-Host "Starting mining node..." -ForegroundColor Yellow
    Start-Process powershell.exe `
        -ArgumentList "-ExecutionPolicy Bypass -File `"$LauncherScript`"" `
        -WindowStyle Normal
    Write-Host "Mining node launched in new window." -ForegroundColor Green
}
