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
!define INSTALLER_MAJOR 0
!define INSTALLER_MINOR 2
!define INSTALLER_PATCH 0
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


Section "$(sec_base_name)" sec_base
	SectionInstType ${it_min} ${it_max} RO

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

LangString sec_base_name ${LANG_ENGLISH} "Drivers"
LangString sec_base_name ${LANG_GERMAN} "Treiber"
LangString sec_dev_name ${LANG_ENGLISH} "Program Development Support"
LangString sec_dev_name ${LANG_GERMAN} "Unterstützung für Anwendungsentwicklung"

LangString desc_sec_base ${LANG_ENGLISH} "Installs components for shared access (multiple processes) to SuperCAN devices."
LangString desc_sec_base ${LANG_GERMAN} "Installiert Komponenten für den Zugriff auf SuperCAN Geräte aus verschiednen Anwendungen heraus."
LangString desc_sec_dev ${LANG_ENGLISH} "Installs header and libraries for application development."
LangString desc_sec_dev ${LANG_GERMAN} "Installiert Header and Bibiotheken für die Anwendungsentwicklung."



;Assign language strings to sections

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN

	!insertmacro MUI_DESCRIPTION_TEXT sec_base "$(desc_sec_base)"
	!insertmacro MUI_DESCRIPTION_TEXT sec_dev "$(desc_sec_dev)"

!insertmacro MUI_FUNCTION_DESCRIPTION_END



;--------------------------------
;Uninstaller Section

Section "Uninstall"

;File ${LICENSE_FILE_PATH}
!insertmacro UnInstallLib REGEXE NOTSHARED NOREBOOT_PROTECTED "$INSTDIR\bin\supercan_srv64.exe"


Delete "$INSTDIR\uninstall.exe"


RMDir /r "$INSTDIR"


;DeleteRegKey /ifempty ${REG_HK} ${REG_PATH}
DeleteRegKey HKLM "${APP_INSTALL_PATH}"



SectionEnd

VIProductVersion ${INSTALLER_MAJOR}.${INSTALLER_MINOR}.${INSTALLER_PATCH}.0
VIFileVersion ${INSTALLER_MAJOR}.${INSTALLER_MINOR}.${INSTALLER_PATCH}.0

VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${INSTALLER_NAME}"
;VIAddVersionKey /LANG=${LANG_ENGLISH} "Comments" "A test comment"
;VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "Fake company"
;VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalTrademarks" "Test Application is a trademark of Fake company"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright (C) 2020, Jean Gressmann. All rights reserved."
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${INSTALLER_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${INSTALLER_MAJOR}.${INSTALLER_MINOR}.${INSTALLER_PATCH}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${INSTALLER_MAJOR}.${INSTALLER_MINOR}.${INSTALLER_PATCH}.0"

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
	ReadRegStr $0 HKLM "${APP_INSTALL_PATH}" "UninstallString"
	ReadRegStr $1 HKLM "${APP_INSTALL_PATH}" "InstallLocation"
	${TrimQuotes} $0 $0
	${TrimQuotes} $1 $1
	;MessageBox MB_OK "$0"
	
	${If} $0 != ""
		;MessageBox MB_OK "runinng uninstall $0"
    ExecWait '"$0" /S _?=$1' $2
		${If} $2 <> 0
			MessageBox MB_ICONQUESTION|MB_YESNO "${SC_NAME} still seems to be installed.$\n$\nContinue with installation?" IDYES next IDNO exit_abort
		${EndIf}
next:	
		Delete "$0"
		RMDir "$1"
	

		; ;ExecWait '"$2" /S _?=$3' $1 ; This assumes the existing uninstaller is a NSIS uninstaller, other uninstallers don't support /S nor _?=
	
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
; exit_continue:
; 	Return
FunctionEnd
