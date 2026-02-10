# Source Path (Your MSYS2 path)
$MsysBin = "C:\msys64\ucrt64\bin"
# Output Directory
$OutDir = ".\LPSM_ControlHub_Dist"

# 1. Create Output Dir
if (!(Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

# 2. DLL List (Based on your ldd output + dependencies)
$DllList = @(
    # C++ Runtime (Critical to fix Anaconda conflict)
    "libstdc++-6.dll",
    "libgcc_s_seh-1.dll",
    "libwinpthread-1.dll",
    
    # Libraries used by lpsm_app
    "libuv-1.dll",          # Used by uWebSockets
    "zlib1.dll",            # Compression
    "libspdlog-1.15.dll",   # Logging
    "libfmt-10.dll"         # Dependency of spdlog (usually needed)
)

Write-Host "Deploying ControlHub files to $OutDir ..." -ForegroundColor Cyan

# 3. Copy DLLs
foreach ($dll in $DllList) {
    $SourcePath = Join-Path $MsysBin $dll
    if (Test-Path $SourcePath) {
        Copy-Item -Path $SourcePath -Destination $OutDir -Force
        Write-Host "  [OK] $dll" -ForegroundColor Green
    } else {
        # Try to find without version number if specific version fails (e.g. libfmt.dll)
        $GenericName = $dll -replace '-\d+\.dll','.dll'
        $GenericPath = Join-Path $MsysBin $GenericName
        
        if (Test-Path $GenericPath) {
             Copy-Item -Path $GenericPath -Destination $OutDir -Force
             Write-Host "  [OK] $GenericName (Fallback)" -ForegroundColor Green
        } else {
             Write-Host "  [WARNING] Not found: $dll (Your app might crash if this is needed)" -ForegroundColor Yellow
        }
    }
}

# 4. Copy EXE
$ExeName = "lpsm_app.exe"
if (Test-Path $ExeName) {
    Copy-Item -Path $ExeName -Destination $OutDir -Force
    Write-Host "  [OK] $ExeName" -ForegroundColor Green
} else {
    Write-Host "  [ERROR] $ExeName not found in current directory!" -ForegroundColor Red
}

# 5. Copy Config
if (Test-Path "config.json") {
    Copy-Item -Path "config.json" -Destination $OutDir -Force
    Write-Host "  [OK] config.json" -ForegroundColor Green
}

# 6. Copy Restart Batch Script (If you created one)
if (Test-Path "Restart_LPSM.bat") {
    Copy-Item -Path "Restart_LPSM.bat" -Destination $OutDir -Force
    Write-Host "  [OK] Restart_LPSM.bat" -ForegroundColor Green
}

Write-Host "`nDone! Please copy [$OutDir] folder to the target machine." -ForegroundColor Yellow