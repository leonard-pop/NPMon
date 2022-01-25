@echo off

set bat_cfgfile=configured.cmd
mkdir build >nul
del %make_cfgfile% %bat_cfgfile% >nul 2>nul
echo [*] Searching for the EWDK drive...
for /f %%d in ('wmic logicaldisk get caption^|findstr :') do (
    if exist %%d\LaunchBuildEnv.cmd (
        echo [-] Found: drive %%d
        echo set EWDK_DRIVE=%%d>%bat_cfgfile%
        goto done_ewdk
        )
    )
echo [!] No EWDK drive found; see README.md for instructions.
goto failed

:done_ewdk

goto done

:failed
del %make_cfgfile% %bat_cfgfile% >nul 2>nul

:done
