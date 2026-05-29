@echo off
setlocal
rem Run from this script's folder so the relative paths in the .sh resolve.
cd /d "%~dp0"

rem Locate Git Bash robustly: standard install dirs first, then PATH.
set "BASH="
if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH if exist "%ProgramFiles(x86)%\Git\bin\bash.exe" set "BASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if not defined BASH for /f "delims=" %%i in ('where bash.exe 2^>nul') do if not defined BASH set "BASH=%%i"

if not defined BASH (
  echo ERROR: Git Bash not found. Install Git for Windows ^(https://git-scm.com^) or add bash to PATH.
  exit /b 1
)

"%BASH%" install-v8-libs.sh
