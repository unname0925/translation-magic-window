; Translation Magic Window 的安裝程式（M5-01，Inno Setup 6）。
;
; 不要直接編譯這個檔案：tools/installer/make_installer.py 會先把要裝的檔案整理到 Stage 資料夾，
; 再用 /DStage=...、/DAppVersion=...、/DOutputDir=... 呼叫 ISCC。
;
; - 裝在使用者自己的 %LOCALAPPDATA%\Programs，不需要系統管理員權限（不會跳出 UAC）
; - 程式本體和預設的 OCR 模型一定會裝；漫畫模式、背景修補、內建字型是可選元件
; - 設定檔、記錄檔在 %APPDATA%、%LOCALAPPDATA% 的 TranslationMagicWindow，解除安裝時不刪（重裝後設定還在）

#ifndef Stage
  #error 請用 tools/installer/make_installer.py 產生安裝程式
#endif

[Setup]
AppId={{6B0F4C2E-5A3B-4C1D-9E77-1D2A8F6C3B90}
AppName=Translation Magic Window
AppVersion={#AppVersion}
AppPublisher=unname0925
AppPublisherURL=https://github.com/unname0925/translation-magic-window
AppSupportURL=https://github.com/unname0925/translation-magic-window/issues
DefaultDirName={localappdata}\Programs\TranslationMagicWindow
DefaultGroupName=Translation Magic Window
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
LicenseFile={#Stage}\main\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=TranslationMagicWindow-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\TranslationMagicWindow.exe
; 程式在執行中時，安裝和解除安裝前先請它結束
CloseApplications=yes

[Languages]
; Inno Setup 沒有附正式的繁體中文翻譯，用我們自己的（make_installer.py 加上 BOM 後放進 Stage）
Name: "chinesetraditional"; MessagesFile: "{#Stage}\ChineseTraditional.isl"

[Types]
Name: "full"; Description: "完整安裝（全部的功能）"
Name: "compact"; Description: "基本安裝（只有一般的文字辨識）"
Name: "custom"; Description: "自訂"; Flags: iscustom

[Components]
Name: "main"; Description: "程式本體和文字辨識模型"; Types: full compact custom; Flags: fixed
Name: "manga"; Description: "漫畫模式（依對話框分段、直排對白用 manga-ocr）"; Types: full
Name: "inpaint"; Description: "背景修補（在原位顯示譯文時把畫在圖上的字抹掉，需要顯示卡）"; Types: full
Name: "fonts"; Description: "內建字型（思源黑體、思源宋體、jf open 粉圓）"; Types: full

[Files]
; solidbreak：每個元件各壓成一塊，沒選的元件安裝時可以整塊跳過（不然只裝基本元件也要解壓全部）
Source: "{#Stage}\main\*"; DestDir: "{app}"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak
Source: "{#Stage}\manga\*"; DestDir: "{app}"; Components: manga; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak
Source: "{#Stage}\inpaint\*"; DestDir: "{app}"; Components: inpaint; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak
Source: "{#Stage}\fonts\*"; DestDir: "{app}"; Components: fonts; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak

[Registry]
; 網頁漫畫擴充功能：Chrome、Edge 從這裡找到主機（tmw_web_host.exe 旁邊的 manifest）。
; 只寫目前使用者的機碼，不需要系統管理員權限；解除安裝時刪掉
Root: HKCU; Subkey: "Software\Google\Chrome\NativeMessagingHosts\io.github.unname0925.tmw"; ValueType: string; ValueName: ""; ValueData: "{app}\io.github.unname0925.tmw.json"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Edge\NativeMessagingHosts\io.github.unname0925.tmw"; ValueType: string; ValueName: ""; ValueData: "{app}\io.github.unname0925.tmw.json"; Flags: uninsdeletekey

[Icons]
Name: "{group}\Translation Magic Window"; Filename: "{app}\TranslationMagicWindow.exe"
Name: "{group}\使用說明"; Filename: "{app}\docs\user-guide.md"
Name: "{group}\解除安裝 Translation Magic Window"; Filename: "{uninstallexe}"
Name: "{userstartup}\Translation Magic Window"; Filename: "{app}\TranslationMagicWindow.exe"; Tasks: startup

[Tasks]
Name: "startup"; Description: "登入 Windows 時自動啟動"; Flags: unchecked

[Run]
Filename: "{app}\TranslationMagicWindow.exe"; Description: "啟動 Translation Magic Window"; Flags: nowait postinstall skipifsilent
