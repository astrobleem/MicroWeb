@ECHO OFF
VRESERVE.COM
IF ERRORLEVEL 1 GOTO FAILED
MICROWEB -video=t %1 %2 %3 %4 %5 %6 %7 %8 %9
GOTO DONE
:FAILED
ECHO Tandy video reservation failed. MicroWeb was not started.
:DONE
