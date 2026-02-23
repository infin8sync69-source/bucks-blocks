# run_tests.ps1 - Build and run all unit and integration tests

Write-Host "Building Test Suite..." -ForegroundColor Cyan

# Create/Update build directory
if (!(Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Push-Location build

# Configure CMake (Generator might need adjustment based on user env, defaulting to MinGW Makefiles as per env)
cmake .. -G "MinGW Makefiles"
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed"
    Pop-Location
    exit 1
}

# Build 'tests' target
cmake --build . --target tests
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    Pop-Location
    exit 1
}

Write-Host "Running Tests..." -ForegroundColor Cyan

# Run CTest
# -V for verbose to see output
ctest -j4 --output-on-failure -C Debug
if ($LASTEXITCODE -ne 0) {
    Write-Error "Tests FAILED"
    Pop-Location
    exit 1
}
else {
    Write-Host "All Tests PASSED" -ForegroundColor Green
}

Pop-Location
