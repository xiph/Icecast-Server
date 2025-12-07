@echo OFF
echo.
echo.
echo|set /p=Starting
.\bin\icecast.exe -v

set CONFIG=icecast.xml
set CONFIG_LOCAL=icecast.local.xml

IF EXIST %CONFIG_LOCAL% (
  set CONFIG=%CONFIG_LOCAL%
)


echo Using config "%CONFIG%" from installation directory ...
echo.
echo Please open http://localhost:8000/ (if you have not changed default ports) in your web browser to see the web interface.
echo.
echo.
echo Leave this window open to keep Icecast running and, if necessary, minimize it.
.\bin\icecast.exe -c %CONFIG%
