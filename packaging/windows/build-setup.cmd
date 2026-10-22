@echo off
setlocal
pushd "%~dp0"

set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (echo csc.exe not found & popd & exit /b 1)
if not exist dist mkdir dist

set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=0.1.0"
if /i "%VERSION:~0,1%"=="v" set "VERSION=%VERSION:~1%"
<nul set /p "=%VERSION%" > dist\VERSION.txt

"%CSC%" /nologo /target:winexe /win32icon:CakeStudio.ico /out:dist\CakeStudio.exe CakeStudioLauncher.cs
if errorlevel 1 (popd & exit /b 1)

copy /y ..\linux\install.sh dist\install.sh >nul
copy /y ..\..\LICENSE dist\LICENSE.txt >nul

"%CSC%" /nologo /target:winexe /platform:anycpu /win32icon:CakeStudio.ico /win32manifest:setup.manifest ^
  /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
  /resource:art\banner.png,banner.png /resource:art\header.png,header.png ^
  /resource:CakeStudio.ico,CakeStudio.ico /resource:TERMS.txt,TERMS.txt ^
  /resource:dist\install.sh,install.sh /resource:dist\CakeStudio.exe,CakeStudio.exe ^
  /resource:dist\LICENSE.txt,LICENSE.txt /resource:dist\VERSION.txt,VERSION.txt ^
  /out:dist\CakeStudioSetup.exe CakeStudioSetup.cs
if errorlevel 1 (popd & exit /b 1)

echo Built %CD%\dist\CakeStudioSetup.exe (version %VERSION%)
popd
