@echo off
pnputil /add-driver "C:\Windows\System32\drivers\MFD.inf" /install
fltmc load MFD
fltmc attach MFD C:
@echo on
C:\Dev\UserConsole