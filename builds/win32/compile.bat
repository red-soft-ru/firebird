@echo off
::==============
:: compile.bat solution, output, [projects...]
::   Note: MSVC7/8 don't accept more than one project
::
::   Note2: Our MSVC8/9 projects create object files in temp/$platform/$config
::     but we call devenv with $config|$platform (note variable in reverse order
::      and odd syntax.) This extended syntax for devenv does not seem to be
::      supported in MSVC7 (despite documentation to the contrary.)
:: 
::   Note3: use msbuild with MSVC15

setlocal
set solution=%1
set output=%2
set projects=

@if "%FB_DBG%"=="" (
	set config=release
) else (
	set config=debug
)

if %MSVC_VERSION% LSS 15 (
	set config="%config%|%FB_TARGET_PLATFORM%"
)

if "%VS_VER_EXPRESS%"=="1" (
	set exec=vcexpress
) else (
	set exec=devenv
)

shift
shift

:loop_start

if "%1" == "" goto loop_end

if %MSVC_VERSION% GEQ 15 (
  set projects=%projects% /target:%1
) else (
  set projects=%projects% /project %1
)

shift
goto loop_start

:loop_end

if %MSVC_VERSION% GEQ 15 (
  msbuild "%solution%.sln" /maxcpucount /p:Configuration=%config% /p:Platform=%FB_TARGET_PLATFORM% %projects% /fileLoggerParameters:LogFile=%output%
) else (
  %exec% %solution%.sln %projects% %FB_CLEAN% %config% /OUT %output%
)

endlocal

goto :EOF
