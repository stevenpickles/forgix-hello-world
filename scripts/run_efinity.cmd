@echo off
setlocal

call "%~1\bin\setup.bat"
if errorlevel 1 exit /b %errorlevel%

cd /d "%~2\fpga"
if errorlevel 1 exit /b %errorlevel%

call efx_run.bat --prj -f compile forgix_hello_world
exit /b %errorlevel%
