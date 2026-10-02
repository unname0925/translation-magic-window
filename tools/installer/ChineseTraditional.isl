; *** Inno Setup 6.5.0+ 繁體中文訊息（台灣） ***
;
; 由 Inno Setup 6.7.3 的 Default.isl 翻譯，給 Translation Magic Window 的安裝程式用。
; tools/installer/make_installer.py 會檢查 Default.isl 的每一則訊息這裡都有。
;
; 注意：原文結尾沒有句號的訊息，翻譯也不要加（Inno Setup 會自己補）。

[LangOptions]
LanguageName=繁體中文
LanguageID=$0404
LanguageCodePage=950
DialogFontName=Microsoft JhengHei UI
DialogFontSize=9
WelcomeFontName=Microsoft JhengHei UI
WelcomeFontSize=14

[Messages]

; *** Application titles
SetupAppTitle=安裝程式
SetupWindowTitle=安裝 - %1
UninstallAppTitle=解除安裝
UninstallAppFullTitle=解除安裝 %1

; *** Misc. common
InformationTitle=資訊
ConfirmTitle=確認
ErrorTitle=錯誤

; *** SetupLdr messages
SetupLdrStartupMessage=即將安裝 %1。要繼續嗎？
LdrCannotCreateTemp=無法建立暫存檔，安裝中止
LdrCannotExecTemp=無法執行暫存資料夾裡的檔案，安裝中止
HelpTextNote=

; *** Startup error messages
LastErrorMessage=%1。%n%n錯誤 %2：%3
SetupFileMissing=安裝資料夾裡少了檔案 %1。請修正問題，或重新取得這個程式。
SetupFileCorrupt=安裝檔已經損壞。請重新取得這個程式。
SetupFileCorruptOrWrongVer=安裝檔已經損壞，或是和這個版本的安裝程式不相容。請修正問題，或重新取得這個程式。
InvalidParameter=命令列中有無效的參數：%n%n%1
SetupAlreadyRunning=安裝程式已經在執行。
WindowsVersionNotSupported=這個程式不支援你的電腦上的 Windows 版本。
WindowsServicePackRequired=這個程式需要 %1 Service Pack %2 或更新的版本。
NotOnThisPlatform=這個程式不能在 %1 上執行。
OnlyOnThisPlatform=這個程式只能在 %1 上執行。
OnlyOnTheseArchitectures=這個程式只能安裝在下列處理器架構的 Windows 上：%n%n%1
WinVersionTooLowError=這個程式需要 %1 %2 或更新的版本。
WinVersionTooHighError=這個程式不能安裝在 %1 %2 或更新的版本上。
AdminPrivilegesRequired=安裝這個程式時，必須以系統管理員身分登入。
PowerUserPrivilegesRequired=安裝這個程式時，必須以系統管理員或 Power Users 群組的成員身分登入。
SetupAppRunningError=安裝程式偵測到 %1 正在執行。%n%n請先把它全部關閉，再按「確定」繼續；或按「取消」離開。
UninstallAppRunningError=解除安裝程式偵測到 %1 正在執行。%n%n請先把它全部關閉，再按「確定」繼續；或按「取消」離開。

; *** Startup questions
PrivilegesRequiredOverrideTitle=選擇安裝模式
PrivilegesRequiredOverrideInstruction=選擇安裝模式
PrivilegesRequiredOverrideText1=%1 可以安裝給所有使用者（需要系統管理員權限），或只安裝給你。
PrivilegesRequiredOverrideText2=%1 可以只安裝給你，或安裝給所有使用者（需要系統管理員權限）。
PrivilegesRequiredOverrideAllUsers=安裝給所有使用者(&A)
PrivilegesRequiredOverrideAllUsersRecommended=安裝給所有使用者（建議）(&A)
PrivilegesRequiredOverrideCurrentUser=只安裝給我(&M)
PrivilegesRequiredOverrideCurrentUserRecommended=只安裝給我（建議）(&M)

; *** Misc. errors
ErrorCreatingDir=安裝程式無法建立資料夾「%1」
ErrorTooManyFilesInDir=資料夾「%1」裡的檔案太多，無法在裡面建立檔案

; *** Setup common messages
ExitSetupTitle=離開安裝程式
ExitSetupMessage=安裝還沒完成。現在離開的話，程式不會被安裝。%n%n你可以之後再執行安裝程式來完成安裝。%n%n要離開安裝程式嗎？
AboutSetupMenuItem=關於安裝程式(&A)...
AboutSetupTitle=關於安裝程式
AboutSetupMessage=%1 版本 %2%n%3%n%n%1 網站：%n%4
AboutSetupNote=
TranslatorNote=

; *** Buttons
ButtonBack=< 上一步(&B)
ButtonNext=下一步(&N) >
ButtonInstall=安裝(&I)
ButtonOK=確定
ButtonCancel=取消
ButtonYes=是(&Y)
ButtonYesToAll=全部都是(&A)
ButtonNo=否(&N)
ButtonNoToAll=全部都否(&O)
ButtonFinish=完成(&F)
ButtonBrowse=瀏覽(&B)...
ButtonWizardBrowse=瀏覽(&R)...
ButtonNewFolder=建立新資料夾(&M)

; *** "Select Language" dialog messages
SelectLanguageTitle=選擇安裝語言
SelectLanguageLabel=選擇安裝過程中要使用的語言。

; *** Common wizard text
ClickNext=按「下一步」繼續，或按「取消」離開安裝程式。
BeveledLabel=
BrowseDialogTitle=瀏覽資料夾
BrowseDialogLabel=在下面的清單中選擇一個資料夾，再按「確定」。
NewFolderName=新資料夾

; *** "Welcome" wizard page
WelcomeLabel1=歡迎使用 [name] 安裝精靈
WelcomeLabel2=即將在你的電腦上安裝 [name/ver]。%n%n建議在繼續之前，先關閉其他所有的應用程式。

; *** "Password" wizard page
WizardPassword=密碼
PasswordLabel1=這個安裝程式受密碼保護。
PasswordLabel3=請輸入密碼，再按「下一步」繼續。密碼會區分大小寫。
PasswordEditLabel=密碼(&P)：
IncorrectPassword=你輸入的密碼不正確，請再試一次。

; *** "License Agreement" wizard page
WizardLicense=授權合約
LicenseLabel=繼續之前，請先閱讀下面的重要資訊。
LicenseLabel3=請閱讀下面的授權合約。你必須接受合約的條款，才能繼續安裝。
LicenseAccepted=我接受合約(&A)
LicenseNotAccepted=我不接受合約(&D)

; *** "Information" wizard pages
WizardInfoBefore=資訊
InfoBeforeLabel=繼續之前，請先閱讀下面的重要資訊。
InfoBeforeClickLabel=準備好繼續安裝時，請按「下一步」。
WizardInfoAfter=資訊
InfoAfterLabel=繼續之前，請先閱讀下面的重要資訊。
InfoAfterClickLabel=準備好繼續安裝時，請按「下一步」。

; *** "User Information" wizard page
WizardUserInfo=使用者資訊
UserInfoDesc=請輸入你的資訊。
UserInfoName=使用者名稱(&U)：
UserInfoOrg=組織(&O)：
UserInfoSerial=序號(&S)：
UserInfoNameRequired=你必須輸入名稱。

; *** "Select Destination Location" wizard page
WizardSelectDir=選擇安裝位置
SelectDirDesc=要把 [name] 安裝在哪裡？
SelectDirLabel3=安裝程式會把 [name] 安裝到下面的資料夾。
SelectDirBrowseLabel=按「下一步」繼續。要選擇其他資料夾，請按「瀏覽」。
DiskSpaceGBLabel=至少需要 [gb] GB 的可用磁碟空間。
DiskSpaceMBLabel=至少需要 [mb] MB 的可用磁碟空間。
CannotInstallToNetworkDrive=安裝程式無法安裝到網路磁碟機。
CannotInstallToUNCPath=安裝程式無法安裝到 UNC 路徑。
InvalidPath=你必須輸入含有磁碟機代號的完整路徑，例如：%n%nC:\APP%n%n或是下面格式的 UNC 路徑：%n%n\\server\share
InvalidDrive=你選擇的磁碟機或 UNC 共用不存在或無法存取，請選擇其他位置。
DiskSpaceWarningTitle=磁碟空間不足
DiskSpaceWarning=安裝程式至少需要 %1 KB 的可用空間，但所選的磁碟機只有 %2 KB 可用。%n%n還是要繼續嗎？
DirNameTooLong=資料夾名稱或路徑太長。
InvalidDirName=資料夾名稱無效。
BadDirName32=資料夾名稱不能包含下列字元：%n%n%1
DirExistsTitle=資料夾已經存在
DirExists=資料夾：%n%n%1%n%n已經存在。還是要安裝到這個資料夾嗎？
DirDoesntExistTitle=資料夾不存在
DirDoesntExist=資料夾：%n%n%1%n%n不存在。要建立這個資料夾嗎？

; *** "Select Components" wizard page
WizardSelectComponents=選擇元件
SelectComponentsDesc=要安裝哪些元件？
SelectComponentsLabel2=勾選要安裝的元件，取消不要安裝的元件。準備好繼續時，請按「下一步」。
FullInstallation=完整安裝
CompactInstallation=精簡安裝
CustomInstallation=自訂安裝
NoUninstallWarningTitle=元件已經存在
NoUninstallWarning=安裝程式偵測到你的電腦上已經安裝了下列元件：%n%n%1%n%n取消選取這些元件不會解除安裝它們。%n%n還是要繼續嗎？
ComponentSize1=%1 KB
ComponentSize2=%1 MB
ComponentsDiskSpaceGBLabel=目前的選擇至少需要 [gb] GB 的磁碟空間。
ComponentsDiskSpaceMBLabel=目前的選擇至少需要 [mb] MB 的磁碟空間。

; *** "Select Additional Tasks" wizard page
WizardSelectTasks=選擇其他工作
SelectTasksDesc=要執行哪些其他工作？
SelectTasksLabel2=選擇安裝 [name] 時要一起執行的其他工作，再按「下一步」。

; *** "Select Start Menu Folder" wizard page
WizardSelectProgramGroup=選擇「開始」功能表資料夾
SelectStartMenuFolderDesc=要把程式的捷徑放在哪裡？
SelectStartMenuFolderLabel3=安裝程式會在下面的「開始」功能表資料夾建立程式的捷徑。
SelectStartMenuFolderBrowseLabel=按「下一步」繼續。要選擇其他資料夾，請按「瀏覽」。
MustEnterGroupName=你必須輸入資料夾名稱。
GroupNameTooLong=資料夾名稱或路徑太長。
InvalidGroupName=資料夾名稱無效。
BadGroupName=資料夾名稱不能包含下列字元：%n%n%1
NoProgramGroupCheck2=不要建立「開始」功能表資料夾(&D)

; *** "Ready to Install" wizard page
WizardReady=準備安裝
ReadyLabel1=安裝程式已經準備好在你的電腦上安裝 [name]。
ReadyLabel2a=按「安裝」繼續安裝；要檢查或變更任何設定，請按「上一步」。
ReadyLabel2b=按「安裝」繼續安裝。
ReadyMemoUserInfo=使用者資訊：
ReadyMemoDir=安裝位置：
ReadyMemoType=安裝類型：
ReadyMemoComponents=選擇的元件：
ReadyMemoGroup=「開始」功能表資料夾：
ReadyMemoTasks=其他工作：

; *** TExtractionWizardPage wizard page and ExtractArchive
DownloadingLabel2=正在下載檔案...
ButtonStopDownload=停止下載(&S)
StopDownload=確定要停止下載嗎？
ErrorDownloadAborted=下載已中止
ErrorDownloadFailed=下載失敗：%1 %2
ErrorDownloadSizeFailed=無法取得大小：%1 %2
ErrorProgress=無效的進度：%1 / %2
ErrorFileSize=檔案大小不對：應該是 %1，實際是 %2

; *** TExtractionWizardPage wizard page and ExtractArchive
ExtractingLabel=正在解壓縮檔案...
ButtonStopExtraction=停止解壓縮(&S)
StopExtraction=確定要停止解壓縮嗎？
ErrorExtractionAborted=解壓縮已中止
ErrorExtractionFailed=解壓縮失敗：%1

; *** Archive extraction failure details
ArchiveIncorrectPassword=密碼不正確
ArchiveIsCorrupted=壓縮檔已經損壞
ArchiveUnsupportedFormat=不支援這種壓縮檔格式

; *** "Preparing to Install" wizard page
WizardPreparing=準備安裝
PreparingDesc=安裝程式正在準備在你的電腦上安裝 [name]。
PreviousInstallNotCompleted=之前的程式安裝或移除還沒完成，需要重新啟動電腦才能完成。%n%n重新啟動電腦之後，請再執行安裝程式來完成 [name] 的安裝。
CannotContinue=安裝程式無法繼續。請按「取消」離開。
ApplicationsFound=下列應用程式正在使用安裝程式需要更新的檔案。建議讓安裝程式自動關閉這些應用程式。
ApplicationsFound2=下列應用程式正在使用安裝程式需要更新的檔案。建議讓安裝程式自動關閉這些應用程式。安裝完成後，安裝程式會試著重新啟動它們。
CloseApplications=自動關閉應用程式(&A)
DontCloseApplications=不要關閉應用程式(&D)
ErrorCloseApplications=安裝程式無法自動關閉所有的應用程式。建議在繼續之前，先關閉所有正在使用安裝程式需要更新的檔案的應用程式。
PrepareToInstallNeedsRestart=安裝程式必須重新啟動你的電腦。重新啟動之後，請再執行安裝程式來完成 [name] 的安裝。%n%n要現在重新啟動嗎？

; *** "Installing" wizard page
WizardInstalling=正在安裝
InstallingLabel=請稍候，安裝程式正在你的電腦上安裝 [name]。

; *** "Setup Completed" wizard page
FinishedHeadingLabel=[name] 安裝精靈完成
FinishedLabelNoIcons=安裝程式已經在你的電腦上安裝好 [name]。
FinishedLabel=安裝程式已經在你的電腦上安裝好 [name]。可以從安裝好的捷徑啟動它。
ClickFinish=按「完成」離開安裝程式。
FinishedRestartLabel=要完成 [name] 的安裝，安裝程式必須重新啟動你的電腦。要現在重新啟動嗎？
FinishedRestartMessage=要完成 [name] 的安裝，安裝程式必須重新啟動你的電腦。%n%n要現在重新啟動嗎？
ShowReadmeCheck=是，我要看說明檔
YesRadio=是，現在重新啟動電腦(&Y)
NoRadio=否，我稍後再重新啟動電腦(&N)
; used for example as 'Run MyProg.exe'
RunEntryExec=執行 %1
; used for example as 'View Readme.txt'
RunEntryShellExec=檢視 %1

; *** "Setup Needs the Next Disk" stuff
ChangeDiskTitle=安裝程式需要下一片磁片
SelectDiskLabel2=請放入第 %1 片磁片，再按「確定」。%n%n如果這片磁片上的檔案可以在下面顯示以外的資料夾找到，請輸入正確的路徑或按「瀏覽」。
PathLabel=路徑(&P)：
FileNotInDir2=在「%2」裡找不到檔案「%1」。請放入正確的磁片，或選擇其他資料夾。
SelectDirectoryLabel=請指定下一片磁片的位置。

; *** Installation phase messages
SetupAborted=安裝沒有完成。%n%n請修正問題後再執行安裝程式。
AbortRetryIgnoreSelectAction=選擇動作
AbortRetryIgnoreRetry=再試一次(&T)
AbortRetryIgnoreIgnore=忽略錯誤並繼續(&I)
AbortRetryIgnoreCancel=取消安裝
RetryCancelSelectAction=選擇動作
RetryCancelRetry=再試一次(&T)
RetryCancelCancel=取消

; *** Installation status messages
StatusClosingApplications=正在關閉應用程式...
StatusCreateDirs=正在建立資料夾...
StatusExtractFiles=正在解壓縮檔案...
StatusDownloadFiles=正在下載檔案...
StatusCreateIcons=正在建立捷徑...
StatusCreateIniEntries=正在建立 INI 項目...
StatusCreateRegistryEntries=正在建立登錄項目...
StatusRegisterFiles=正在註冊檔案...
StatusSavingUninstall=正在儲存解除安裝資訊...
StatusRunProgram=正在完成安裝...
StatusRestartingApplications=正在重新啟動應用程式...
StatusRollback=正在復原變更...

; *** Misc. errors
ErrorInternal2=內部錯誤：%1
ErrorFunctionFailedNoCode=%1 失敗
ErrorFunctionFailed=%1 失敗；代碼 %2
ErrorFunctionFailedWithMessage=%1 失敗；代碼 %2。%n%3
ErrorExecutingProgram=無法執行檔案：%n%1

; *** Registry errors
ErrorRegOpenKey=開啟登錄機碼時發生錯誤：%n%1\%2
ErrorRegCreateKey=建立登錄機碼時發生錯誤：%n%1\%2
ErrorRegWriteKey=寫入登錄機碼時發生錯誤：%n%1\%2

; *** INI errors
ErrorIniEntry=在檔案「%1」建立 INI 項目時發生錯誤。

; *** File copying errors
FileAbortRetryIgnoreSkipNotRecommended=略過這個檔案（不建議）(&S)
FileAbortRetryIgnoreIgnoreNotRecommended=忽略錯誤並繼續（不建議）(&I)
SourceIsCorrupted=來源檔已經損壞
SourceDoesntExist=來源檔「%1」不存在
SourceVerificationFailed=來源檔驗證失敗：%1
VerificationSignatureDoesntExist=簽章檔「%1」不存在
VerificationSignatureInvalid=簽章檔「%1」無效
VerificationKeyNotFound=簽章檔「%1」使用了不認得的金鑰
VerificationFileNameIncorrect=檔案名稱不正確
VerificationFileTagIncorrect=檔案的標記不正確
VerificationFileSizeIncorrect=檔案大小不正確
VerificationFileHashIncorrect=檔案的雜湊值不正確
ExistingFileReadOnly2=現有的檔案被設成唯讀，無法取代。
ExistingFileReadOnlyRetry=移除唯讀屬性後再試一次(&R)
ExistingFileReadOnlyKeepExisting=保留現有的檔案(&K)
ErrorReadingExistingDest=讀取現有的檔案時發生錯誤：
FileExistsSelectAction=選擇動作
FileExists2=檔案已經存在。
FileExistsOverwriteExisting=覆寫現有的檔案(&O)
FileExistsKeepExisting=保留現有的檔案(&K)
FileExistsOverwriteOrKeepAll=接下來的衝突都這樣處理(&D)
ExistingFileNewerSelectAction=選擇動作
ExistingFileNewer2=現有的檔案比安裝程式要安裝的還新。
ExistingFileNewerOverwriteExisting=覆寫現有的檔案(&O)
ExistingFileNewerKeepExisting=保留現有的檔案（建議）(&K)
ExistingFileNewerOverwriteOrKeepAll=接下來的衝突都這樣處理(&D)
ErrorChangingAttr=變更現有檔案的屬性時發生錯誤：
ErrorCreatingTemp=在安裝資料夾建立檔案時發生錯誤：
ErrorReadingSource=讀取來源檔時發生錯誤：
ErrorCopying=複製檔案時發生錯誤：
ErrorDownloading=下載檔案時發生錯誤：
ErrorExtracting=解壓縮時發生錯誤：
ErrorReplacingExistingFile=取代現有的檔案時發生錯誤：
ErrorRestartReplace=RestartReplace 失敗：
ErrorRenamingTemp=在安裝資料夾重新命名檔案時發生錯誤：
ErrorRegisterServer=無法註冊 DLL/OCX：%1
ErrorRegSvr32Failed=RegSvr32 失敗，結束代碼 %1
ErrorRegisterTypeLib=無法註冊型別程式庫：%1

; *** Uninstall display name markings
UninstallDisplayNameMark=%1（%2）
UninstallDisplayNameMarks=%1（%2，%3）
UninstallDisplayNameMark32Bit=32 位元
UninstallDisplayNameMark64Bit=64 位元
UninstallDisplayNameMarkAllUsers=所有使用者
UninstallDisplayNameMarkCurrentUser=目前的使用者

; *** Post-installation errors
ErrorOpeningReadme=開啟說明檔時發生錯誤。
ErrorRestartingComputer=安裝程式無法重新啟動電腦，請自行重新啟動。

; *** Uninstaller messages
UninstallNotFound=檔案「%1」不存在，無法解除安裝。
UninstallOpenError=無法開啟檔案「%1」，無法解除安裝
UninstallUnsupportedVer=解除安裝記錄檔「%1」的格式這個版本的解除安裝程式看不懂，無法解除安裝
UninstallUnknownEntry=解除安裝記錄裡有不認得的項目（%1）
ConfirmUninstall=確定要完全移除 %1 和它的所有元件嗎？
UninstallOnlyOnWin64=這個安裝只能在 64 位元的 Windows 上解除安裝。
OnlyAdminCanUninstall=只有具備系統管理員權限的使用者才能解除安裝這個程式。
UninstallStatusLabel=請稍候，正在從你的電腦移除 %1。
UninstalledAll=已經從你的電腦移除 %1。
UninstalledMost=%1 解除安裝完成。%n%n有些項目無法移除，可以自行刪除。
UninstalledAndNeedsRestart=要完成 %1 的解除安裝，必須重新啟動你的電腦。%n%n要現在重新啟動嗎？
UninstallDataCorrupted=檔案「%1」已經損壞，無法解除安裝

; *** Uninstallation phase messages
ConfirmDeleteSharedFileTitle=移除共用檔案？
ConfirmDeleteSharedFile2=系統顯示下列共用檔案已經沒有程式在使用。要讓解除安裝程式移除這個共用檔案嗎？%n%n如果還有程式在使用這個檔案，移除後那些程式可能無法正常運作。不確定的話請選「否」，把檔案留在系統上不會造成任何問題。
SharedFileNameLabel=檔案名稱：
SharedFileLocationLabel=位置：
WizardUninstalling=解除安裝狀態
StatusUninstalling=正在解除安裝 %1...

; *** Shutdown block reasons
ShutdownBlockReasonInstallingApp=正在安裝 %1。
ShutdownBlockReasonUninstallingApp=正在解除安裝 %1。

[CustomMessages]

NameAndVersion=%1 版本 %2
AdditionalIcons=其他捷徑：
CreateDesktopIcon=建立桌面捷徑(&D)
CreateQuickLaunchIcon=建立快速啟動捷徑(&Q)
ProgramOnTheWeb=%1 網站
UninstallProgram=解除安裝 %1
LaunchProgram=啟動 %1
AssocFileExtension=把 %1 和副檔名 %2 建立關聯(&A)
AssocingFileExtension=正在把 %1 和副檔名 %2 建立關聯...
AutoStartProgramGroupDescription=啟動：
AutoStartProgram=自動啟動 %1
AddonHostProgramNotFound=在你選擇的資料夾裡找不到 %1。%n%n還是要繼續嗎？
