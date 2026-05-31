@echo off
setlocal

echo Building terk-krypton (pure Krypton native PE/COFF)...
kcc.exe -o terk.exe run.k
if errorlevel 1 (
    echo ERROR: kcc failed.
    exit /b 1
)

echo Done. Run terk.exe to launch.
