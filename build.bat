set cfgfile=%~dp0configured.cmd
if not exist %cfgfile% call configure.bat
call "%cfgfile%"
if exist custom_configured.cmd call custom_configured.cmd
call "%EWDK_DRIVE%\BuildEnv\SetupBuildEnv.cmd"
msbuild.exe %*
