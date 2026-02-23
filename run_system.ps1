# run_system.ps1 - Run System/Full-Node simulation tests

Write-Host "Running System Tests..." -ForegroundColor Cyan

if (!(Test-Path "build")) {
    Write-Error "Build directory not found. Run run_tests.ps1 first to build."
    exit 1
}

Push-Location build

# Run system tests (Full Node startup, etc.)
ctest -R "system" --verbose

if ($LASTEXITCODE -ne 0) {
    Write-Error "System tests FAILED."
}
else {
    Write-Host "System tests PASSED." -ForegroundColor Green
}

Pop-Location
