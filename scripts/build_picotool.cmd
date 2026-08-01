@echo off
setlocal EnableExtensions

if not defined PICOTOOL_SOURCE_NATIVE exit /b 2
if not defined PICOTOOL_BUILD_NATIVE exit /b 2
if not defined PICOTOOL_INSTALL_NATIVE exit /b 2
if not defined PICO_SDK_PATH_NATIVE exit /b 2
if not defined LIBUSB_ROOT_NATIVE exit /b 2
if not defined LIBUSB_INCLUDE_NATIVE exit /b 2
if not defined LIBUSB_LIBRARY_NATIVE exit /b 2
if not defined CMAKE_EXE_NATIVE exit /b 2
if not defined VSDEVCMD_NATIVE exit /b 2

call "%VSDEVCMD_NATIVE%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

echo Configuring picotool 2.3.0 with USB support...
"%CMAKE_EXE_NATIVE%" --fresh ^
  -S "%PICOTOOL_SOURCE_NATIVE%" ^
  -B "%PICOTOOL_BUILD_NATIVE%" ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPICO_SDK_PATH:PATH="%PICO_SDK_PATH_NATIVE%" ^
  -DLIBUSB_ROOT:PATH="%LIBUSB_ROOT_NATIVE%" ^
  -DLIBUSB_INCLUDE_DIR:PATH="%LIBUSB_INCLUDE_NATIVE%" ^
  -DLIBUSB_LIBRARIES:FILEPATH="%LIBUSB_LIBRARY_NATIVE%" ^
  -DPICOTOOL_NO_LIBUSB:BOOL=OFF ^
  -DPICOTOOL_FLAT_INSTALL:BOOL=ON ^
  -DCMAKE_INSTALL_PREFIX:PATH="%PICOTOOL_INSTALL_NATIVE%"
if errorlevel 1 exit /b %errorlevel%

echo Building picotool 2.3.0...
"%CMAKE_EXE_NATIVE%" --build "%PICOTOOL_BUILD_NATIVE%"
if errorlevel 1 exit /b %errorlevel%

echo Installing picotool 2.3.0...
"%CMAKE_EXE_NATIVE%" --install "%PICOTOOL_BUILD_NATIVE%"
if errorlevel 1 exit /b %errorlevel%

set "PICOTOOL_EXE=%PICOTOOL_INSTALL_NATIVE%\picotool\picotool.exe"
if not exist "%PICOTOOL_EXE%" (
  echo Installed picotool executable was not found: %PICOTOOL_EXE% 1>&2
  exit /b 1
)

"%PICOTOOL_EXE%" version >"%PICOTOOL_BUILD_NATIVE%\picotool-version.txt" 2>&1
if errorlevel 1 exit /b %errorlevel%
type "%PICOTOOL_BUILD_NATIVE%\picotool-version.txt"

findstr /C:"picotool v2.3.0" "%PICOTOOL_BUILD_NATIVE%\picotool-version.txt" >nul
if errorlevel 1 (
  echo Installed executable is not picotool 2.3.0. 1>&2
  exit /b 1
)

findstr /C:"compiled without USB support" "%PICOTOOL_BUILD_NATIVE%\picotool-version.txt" >nul
if not errorlevel 1 (
  echo Installed picotool was built without USB support. 1>&2
  exit /b 1
)

"%PICOTOOL_EXE%" help load >nul 2>&1
if errorlevel 1 (
  echo Installed picotool does not provide the USB load command. 1>&2
  exit /b 1
)

echo USB command verification passed: %PICOTOOL_EXE%
exit /b 0
