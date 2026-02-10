cd /c/Users/Peter_Wang/Desktop/Temp/CPlusPlusScripts/ControlHub/build
rm -rf *
cmake .. -G "MinGW Makefiles"
cmake --build .
cp ../DLLs/Bundle-MsysApp.ps1 ./
PowerShell.exe -ExecutionPolicy Bypass -File .\Bundle-MsysApp.ps1 -Target ".\lpsm_app.exe"