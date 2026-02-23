$env:Path = "c:\Users\shafe\OneDrive\Desktop\Blockchain\tools\mingw64\bin;C:\Program Files\OpenSSL-Win64\bin;$env:Path"

# Change to build directory so relative paths (like ../web) work correctly
Set-Location -Path "build"

if ($args.Count -eq 0) {
    Write-Host "Starting Bucks Server..."
    .\bucks.exe --server --web-root ../web
}
else {
    Write-Host "Running Bucks with arguments: $args"
    .\bucks.exe @args --web-root ../web
}

# Restore location (optional, but good practice if running in kept-alive shell)
Set-Location -Path ".."
