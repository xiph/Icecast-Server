@echo OFF
echo.
echo.
echo|set /p=Starting
.\bin\icecast.exe -v
echo Using config "icecast.xml" from installation directory ...
echo.
echo Please open http://localhost:8000 in your web browser to see the web interface.
echo.
echo.
echo Leave this window open to keep Icecast running and, if necessary, minimize it.
echo.
.\bin\icecast.exe -c .\icecast.xml
