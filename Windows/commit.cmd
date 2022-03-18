@echo off
FOR /F "delims=" %%i IN ('git rev-parse --short HEAD') DO echo #define SC_COMMIT "%%i"
