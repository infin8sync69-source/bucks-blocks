# run_perf.ps1 - Run Performance benchmarks and Security fuzz tests

Write-Host "Running Performance & Security Tests..." -ForegroundColor Cyan

if (!(Test-Path "build")) {
    Write-Error "Build directory not found. Run run_tests.ps1 first to build."
    exit 1
}

Push-Location build

# filter regex matches "perf" or "fuzz" or the executable name
ctest -R "perf_security" --verbose

if ($LASTEXITCODE -ne 0) {
    Write-Warning "Some performance/security tests failed or triggered alerts."
}
else {
    Write-Host "Performance checks completed successfully." -ForegroundColor Green
}

Pop-Location
