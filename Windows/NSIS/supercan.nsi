!define LIBRARY_X64 1

;--------------------------------
!include "MUI2.nsh"
!include "Library.nsh"
!include "FileFunc.nsh"


;--------------------------------

;General
!define SC_NAME "SuperCAN"
!define SC_GUID "{8688E45F-740D-4B9D-B876-9650E29F2AD7}"
!define LICENSE_FILE_PATH ..\..\LICENSE
!define INSTALLER_NAME "${SC_NAME}"
!define APP_INSTALL_PATH "Software\Microsoft\Windows\CurrentVersion\Uninstall\${SC_GUID}"


!ifndef SC_VERSION_MAJOR
  !define SC_VERSION_MAJOR 0
!endif

!ifndef SC_VERSION_MINOR
  !define SC_VERSION_MINOR 0
!endif

!ifndef SC_VERSION_PATCH
  !define SC_VERSION_PATCH 0
!endif

!ifndef SC_VERSION_BUILD
  !define SC_VERSION_BUILD 0
!endif

!define INSTALLER_MAJOR 1
!define INSTALLER_MINOR 0
!define INSTALLER_PATCH 0
!define INSTALLER_BUILD ${SC_VERSION_BUILD}



!define WriteAppInstallKeyStr "!insertmacro _WriteAppInstallKeyStr"
!macro _WriteAppInstallKeyStr key value
	WriteRegStr \
		HKLM \
		"${APP_INSTALL_PATH}" \
		"${key}" \
		"${value}"
!macroend

!define WriteAppInstallKeyDWORD "!insertmacro _WriteAppInstallKeyDWORD"
!macro _WriteAppInstallKeyDWORD key value
	WriteRegDWORD \
		HKLM \
		"${APP_INSTALL_PATH}" \
		"${key}" \
		"${value}"
!macroend

Var Reboot

SetCompressor /SOLID lzma
;[/SOLID] [/FINAL] zlib|bzip2|lzma




ManifestSupportedOS Win10
Name  "${INSTALLER_NAME}"
OutFile "supercan_inst.exe"
Unicode True



;Default installation folder
InstallDir "$PROGRAMFILES64\${SC_NAME}"

;Get installation folder from registry if available
InstallDirRegKey HKLM "${APP_INSTALL_PATH" "InstallLocation"



;Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------

;Interface Settings
!define MUI_ABORTWARNING



;--------------------------------

;Pages

!insertmacro MUI_PAGE_LICENSE ${LICENSE_FILE_PATH}
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES



;--------------------------------

;Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"



;--------------------------------

;Installer Sections

InstType "$(it_min)" it_min
InstType "$(it_max)" it_max

!define VcRuntimeInstall "!insertmacro _VcRuntimeInstall"
!macro _VcRuntimeInstall arch
	File "$%VCToolsRedistDir%\vc_redist.${arch}.exe"

	StrCpy $1 "$\"$TEMP\vc_redist.${arch}.exe$\" /install /passive /norestart"
	IfSilent +1 +2
		StrCpy $1 "$1 /quiet"

	ExecWait "$1" $0
	Delete "$TEMP\vc_redist.${arch}.exe"
	${If} $0 <> 0
		StrCpy $2 "exit code $0"
		DetailPrint $2
		${If} $0 == 3010 ; restart required
			IntOp $Reboot $Reboot | 1
		${Else}
			MessageBox MB_ICONQUESTION|MB_YESNO "$(mb_query_continue_after_vc_redist_failed)" /SD IDNO IDYES next_${arch} ;IDNO exit_abort_${arch}
;exit_abort_${arch}:
			;DetailPrint "Abort"
			Abort
		${EndIf}
	${EndIf}

next_${arch}:
	DetailPrint "$(info_vc_redist_installed) (${arch})"

!macroend


Section "$(sec_base_name)" sec_base
	SectionInstType ${it_min} ${it_max} RO

	SetOutPath "$TEMP"
	${VcRuntimeInstall} x86
	${VcRuntimeInstall} x64

	SetOutPath "$INSTDIR"
	File ${LICENSE_FILE_PATH}

	SetOutPath "$INSTDIR\bin"
	File ..\Win32\Release\supercan32.dll
	File ..\Win32\Release\supercan_app32.exe
	File ..\x64\Release\supercan64.dll
	File ..\x64\Release\supercan_app64.exe

	!insertmacro InstallLib REGEXE NOTSHARED NOREBOOT_PROTECTED ..\x64\Release\supercan_srv64.exe "$INSTDIR\bin\supercan_srv64.exe" "$INSTDIR\tmp"


	;Store installation folder
	${WriteAppInstallKeyStr} "DisplayName" "$(xinst_name)"
	${WriteAppInstallKeyStr} "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
	${WriteAppInstallKeyStr} "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
	${WriteAppInstallKeyStr} "InstallLocation" "$\"$INSTDIR$\""
	${WriteAppInstallKeyStr} "Publisher" "Jean Gressmann"
	${WriteAppInstallKeyStr} "URLUpdateInfo" "https://github.com/jgressmann/supercan"
	${WriteAppInstallKeyStr} "URLInfoAbout" "https://github.com/jgressmann/supercan"
	${WriteAppInstallKeyStr} "DisplayVersion" "${SC_VERSION_MAJOR}.${SC_VERSION_MINOR}.${SC_VERSION_PATCH}"
	${WriteAppInstallKeyDWORD} "VersionMajor" "${SC_VERSION_MAJOR}"
	${WriteAppInstallKeyDWORD} "VersionMinor" "${SC_VERSION_MINOR}"
	${WriteAppInstallKeyDWORD} "NoModify" "1"
	${WriteAppInstallKeyDWORD} "NoRepair" "1"

	WriteUninstaller "$INSTDIR\uninstall.exe"

SectionEnd

Section /o "$(sec_dev_name)" sec_dev
	SectionInstType ${it_max}

	SetOutPath "$INSTDIR\inc"
	File /r ..\inc\*

	SetOutPath "$INSTDIR\lib"
	File ..\Win32\Release\supercan32.lib
	File ..\x64\Release\supercan64.lib

	SetOutPath "$INSTDIR\src"
	File ..\..\src\supercan.h
	File ..\..\src\supercan_misc.h
	File ..\..\src\can_bit_timing.*

SectionEnd

Section "" sec_hidden
	SectionInstType ${it_min} ${it_max}

	${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
 	IntFmt $0 "0x%08X" $0
	${WriteAppInstallKeyDWORD} "EstimatedSize" "$0"

SectionEnd



;--------------------------------



;Language strings
LangString xinst_name ${LANG_ENGLISH} "${SC_NAME} Device Driver"
LangString xinst_name ${LANG_GERMAN} "${SC_NAME} Gerätetreiber"

LangString it_min ${LANG_ENGLISH} "Minimal Install"
LangString it_min ${LANG_GERMAN} "Minimale Installation"
LangString it_max ${LANG_ENGLISH} "Full Install"
LangString it_max ${LANG_GERMAN} "Vollständige Installation"
LangString vc_redist ${LANG_ENGLISH} "Microsoft Visual C++ Redistributable"
LangString vc_redist ${LANG_GERMAN} "Microsoft Visual C++ Redistributable"



LangString sec_base_name ${LANG_ENGLISH} "Drivers"
LangString sec_base_name ${LANG_GERMAN} "Treiber"
LangString sec_dev_name ${LANG_ENGLISH} "Program Development Support"
LangString sec_dev_name ${LANG_GERMAN} "Unterstützung für Anwendungsentwicklung"

LangString desc_sec_base ${LANG_ENGLISH} "Installs components for shared access (multiple processes) to SuperCAN devices."
LangString desc_sec_base ${LANG_GERMAN} "Installiert Komponenten für den Zugriff auf SuperCAN Geräte aus verschiedenen Anwendungen heraus."
LangString desc_sec_dev ${LANG_ENGLISH} "Installs header and libraries for application development."
LangString desc_sec_dev ${LANG_GERMAN} "Installiert Header and Bibliotheken für die Anwendungsentwicklung."

LangString mb_query_continue_installation ${LANG_ENGLISH} "${SC_NAME} still seems to be installed.$\n$\nContinue with installation?"
LangString mb_query_continue_installation ${LANG_GERMAN} "${SC_NAME} scheint nicht installiert zu sein.$\n$\nMit dieser Installation fortfahren?"
LangString mb_query_continue_after_vc_redist_failed ${LANG_ENGLISH} "$(vc_redist) failed to install.$\n$\nContinue with ${SC_NAME} installation?"
LangString mb_query_continue_after_vc_redist_failed ${LANG_GERMAN} "Die Installation des $(vc_redist) ist fehlgeschlagen.$\n$\nMit der Installation von ${SC_NAME} fortfahren?"

LangString info_vc_redist_installed ${LANG_ENGLISH} "$(vc_redist) installed successfully."
LangString info_vc_redist_installed ${LANG_GERMAN} "$(vc_redist) erfolgreich installiert."

LangString mb_install_requires_reboot ${LANG_ENGLISH} "${SC_NAME} installed successfully.$\nPlease reboot your machine to complete the process."
LangString mb_install_requires_reboot ${LANG_GERMAN} "${SC_NAME} installiert.$\nBitte starten Sie Ihren Rechner neu um den Prozess abzuschliessen."

;Assign language strings to sections

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN

	!insertmacro MUI_DESCRIPTION_TEXT sec_base "$(desc_sec_base)"
	!insertmacro MUI_DESCRIPTION_TEXT sec_dev "$(desc_sec_dev)"

!insertmacro MUI_FUNCTION_DESCRIPTION_END



;--------------------------------
;Uninstaller Section

Section "Uninstall"

	!insertmacro UnInstallLib REGEXE NOTSHARED NOREBOOT_PROTECTED "$INSTDIR\bin\supercan_srv64.exe"

	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\uninstall.exe"

	RMDir /r "$INSTDIR\bin"
	RMDir /r "$INSTDIR\inc"
	RMDir /r "$INSTDIR\lib"
	RMDir /r "$INSTDIR\src"

	RMDir "$INSTDIR"

	DeleteRegKey HKLM "${APP_INSTALL_PATH}"

SectionEnd

VIProductVersion ${SC_VERSION_MAJOR}.${SC_VERSION_MINOR}.${SC_VERSION_PATCH}.${SC_VERSION_BUILD}
VIFileVersion ${INSTALLER_MAJOR}.${INSTALLER_MINOR}.${INSTALLER_PATCH}.${INSTALLER_BUILD}

VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${INSTALLER_NAME}"
;VIAddVersionKey /LANG=${LANG_ENGLISH} "Comments" "A test comment"
;VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "Fake company"
;VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalTrademarks" "Test Application is a trademark of Fake company"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright (c) 2020-2021, Jean Gressmann. All rights reserved."
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${INSTALLER_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${SC_VERSION_MAJOR}.${SC_VERSION_MINOR}.${SC_VERSION_PATCH}.${SC_VERSION_BUILD}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${INSTALLER_MAJOR}.${INSTALLER_MINOR}.${INSTALLER_PATCH}.${INSTALLER_BUILD}"

;https://nsis.sourceforge.io/Trim_quotes
Function TrimQuotes
Exch $R0
Push $R1

  StrCpy $R1 $R0 1
  StrCmp $R1 `"` 0 +2
    StrCpy $R0 $R0 `` 1
  StrCpy $R1 $R0 1 -1
  StrCmp $R1 `"` 0 +2
    StrCpy $R0 $R0 -1

Pop $R1
Exch $R0
FunctionEnd

!macro _TrimQuotes Input Output
  Push `${Input}`
  Call TrimQuotes
  Pop ${Output}
!macroend
!define TrimQuotes `!insertmacro _TrimQuotes`

Function .onInit
	IntOp $Reboot $Reboot ^ $Reboot

	ReadRegStr $0 HKLM "${APP_INSTALL_PATH}" "UninstallString"
	ReadRegStr $1 HKLM "${APP_INSTALL_PATH}" "InstallLocation"
	${TrimQuotes} $0 $0
	${TrimQuotes} $1 $1
	;MessageBox MB_OK "$0"

	${If} $0 != ""
		;MessageBox MB_OK "runinng uninstall $0"
    	ExecWait '"$0" /S' $2
		${If} $2 <> 0
			MessageBox MB_ICONQUESTION|MB_YESNO "$(mb_query_continue_installation)" /SD IDYES IDYES next IDNO exit_abort
		${EndIf}
next:
		; the installer can't delete itself when invoked like this
		;Delete "$0"
		;RMDir "$1"
		; ClearErrors
		; RMDir "$1"
		; ClearErrors

		;;ExecWait '"$2" /S _?=$3' $1 ; This assumes the existing uninstaller is a NSIS uninstaller, other uninstallers don't support /S nor _?=

		; ;ExecShellWait "" '"$0"'
		; IfFileExists $0 0 exit_abort
		; ;ReadRegStr $0 HKLM "${APP_INSTALL_PATH}" "UninstallString"
		; ;${If} $0 != ""
		; 	MessageBox MB_OK "still have $0, exit code=$2"


			;Goto +1
		;${EndIf}
		; ${If} $1 <> 0

		; ${EndIf}

	;${Else}
		;MessageBox MB_OK "no previous installation"
		;DetailPrint "no previous installation"
	${EndIf}

	Return
exit_abort:
	Abort
FunctionEnd

Function .onInstSuccess
	IfSilent out
	${If} $Reboot & 1
		MessageBox MB_OK "$(mb_install_requires_reboot)"
	${EndIf}
out:
	Return
FunctionEnd
