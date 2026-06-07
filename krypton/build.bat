@echo off
setlocal

echo Building brain-krypton (pure Krypton native PE/COFF)...
kcc.exe -o brain.exe run.k
if errorlevel 1 (
    echo ERROR: kcc failed.
    exit /b 1
)

echo Done. Run brain.exe to launch.
