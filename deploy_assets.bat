@echo off
set "QT_ROOT=C:\Qt\5.15.2\mingw81_64"
set "MINGW_BIN=C:\Qt\Tools\mingw810_64\bin"
set "DEST=%~dp1"

echo Automating DLL collection for %1...

:: Copy core Qt DLLs
copy /y "%QT_ROOT%\bin\Qt5Core.dll" "%DEST%"
copy /y "%QT_ROOT%\bin\Qt5Gui.dll" "%DEST%"
copy /y "%QT_ROOT%\bin\Qt5Widgets.dll" "%DEST%"
copy /y "%QT_ROOT%\bin\Qt5Svg.dll" "%DEST%"

:: Copy MinGW runtimes
copy /y "%MINGW_BIN%\libgcc_s_seh-1.dll" "%DEST%"
copy /y "%MINGW_BIN%\libstdc++-6.dll" "%DEST%"
copy /y "%MINGW_BIN%\libwinpthread-1.dll" "%DEST%"
copy /y "%MINGW_BIN%\libgomp-1.dll" "%DEST%"

:: Copy platform plugin (Required for GUI)
if not exist "%DEST%platforms" mkdir "%DEST%platforms"
copy /y "%QT_ROOT%\plugins\platforms\qwindows.dll" "%DEST%platforms\"

echo Deployment complete.
