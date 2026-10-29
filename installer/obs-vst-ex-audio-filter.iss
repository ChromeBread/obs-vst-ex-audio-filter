; Inno Setup 6.6+ script. Build: cmake --install to a stage folder, then
;   iscc /DAppVersion=1.2.0 /DStageDir=<stage> installer\obs-vst-ex-audio-filter.iss
; Installs two OBS modules: VST Ex (obs-vst-ex-audio-filter) and VST Enigmax Loader (vst-enigmax-loader).
; The custom setup's choices go to install-features.ini, which VST Ex reads once at its next load.
; Uninstall asks what to keep; a backup it makes has the same layout as Tools > Debug mode > Backup.

#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif
#ifndef StageDir
  #define StageDir "..\build_x64\stage"
#endif
#define MainName "obs-vst-ex-audio-filter"
#define LoaderName "vst-enigmax-loader"
#define FeatureIni "{userappdata}\obs-studio\plugin_config\" + MainName + "\install-features.ini"

[Setup]
AppId={{6B0E7C51-3F2A-4D8B-9E55-1C7A0F4B2D93}
AppName=OBS VST Ex Audio Filter
AppVersion={#AppVersion}
AppPublisher=BlackDream feat. ChromeBread
AppPublisherURL=https://github.com/ChromeBread/obs-vst-ex-audio-filter
; {app} is OBS's plugin folder; each module gets its own subfolder there.
DefaultDirName={commonappdata}\obs-studio\plugins
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={app}\{#MainName}\uninstall
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir=..\build_x64\installer
OutputBaseFilename={#MainName}-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Wide enough that the long component descriptions are not cut off.
WizardSizePercent=150,120
WizardResizable=yes
ShowLanguageDialog=auto

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"

[CustomMessages]
en.TypeFull=Full installation
en.TypeCustom=Custom installation
en.PartCore=VST Ex Audio Filter (required): Filters (VST Ex) in the right-click menu, pitch / pseudo-stereo filters, Tools > VST Ex Audio Filter
en.PartLoader=VST Enigmax Loader: loads VST2 / VST3 plugins (safe scan, plugin browser, editor screenshots, settings export / import)
en.PartChainView=VST Ex Chain View: shows the VST chain of every audio source (devices, filters, latency, levels) with a sound imager. Tools menu and dock button
en.PartMixer=VST Ex Mixer dock: under each Audio Mixer meter, one button per VST filter of that source. Click: ON/OFF, right-click: open the plugin
en.PartSwitcher=VST Ex Switcher dock: ON/OFF set buttons, and CPU usage of OBS with the share used by VST plugins
en.CloseObs=OBS is running. Quit OBS, then click Retry.
en.LoaderFiltersLost=Your scene collections have %1 VST Enigmax Loader filter(s). Without VST Enigmax Loader they disappear from OBS.%n%nTo keep them, first move them to OBS VST 2.x / atkAudio in OBS: Tools > VST Enigmax Loader > Debug mode > Hand over.%n%nStop now to do that?
en.KeepTitle=What should be removed?
en.KeepIntro=Choose what to delete. Anything left unticked stays on this PC.
en.DelThumbs=Plugin thumbnails (low resolution)
en.DelShots=High-resolution screenshots
en.DelSettings=Settings, chain presets and filter history
en.ExportFirst=Export everything before deleting
en.ExportWhere=Folder for the backup (a VST-Ex-backup folder is created inside):
en.OK=OK
en.Strays=These files in the plugin's folders were not made by it (other files, or renamed copies such as .bak / .org). Tick the ones to delete; the rest are kept.
en.DeleteTicked=Delete ticked
en.KeepAll=Keep all
en.Exported=Backup saved to:%n%1
ja.TypeFull=フル インストール
ja.TypeCustom=カスタム インストール
ja.PartCore=VST Ex Audio Filter (必須): 右クリックの フィルタ (VST Ex)、ピッチ / 疑似ステレオ フィルタ、ツール > VST Ex Audio Filter
ja.PartLoader=VST Enigmax Loader: VST2 / VST3 プラグインの読み込み (安全なスキャン、プラグインブラウザ、エディタのスクリーンショット、設定のエクスポート / インポート)
ja.PartChainView=VST Ex Chain View: 全オーディオソースの VST チェーン (デバイス・フィルタ・レイテンシ・レベル) の表示とサウンドイメージャー。ツールメニューとドックのボタン
ja.PartMixer=VST Ex Mixer ドック: オーディオミキサーの各メーターの下に、そのソースの VST フィルタごとのボタン。クリックで ON/OFF、右クリックでプラグインを開く
ja.PartSwitcher=VST Ex Switcher ドック: ON/OFF セットのボタンと、OBS の CPU 使用率のうち VST プラグインが使っている割合の表示
ja.CloseObs=OBS が起動しています。OBS を終了してから「再試行」を押してください。
ja.LoaderFiltersLost=シーンコレクションに VST Enigmax Loader のフィルタが %1 個あります。VST Enigmax Loader がないと OBS から消えます。%n%n残すには、先に OBS の ツール > VST Enigmax Loader > デバッグモード > 引き継ぎ で OBS VST 2.x / atkAudio へ移してください。%n%nいったん中止して移しますか?
ja.KeepTitle=何を削除しますか?
ja.KeepIntro=削除するものを選んでください。チェックしなかったものはこの PC に残ります。
ja.DelThumbs=プラグインのサムネイル (低解像度)
ja.DelShots=高解像度スクリーンショット
ja.DelSettings=設定・チェーンのプリセット・フィルタの履歴
ja.ExportFirst=全てエクスポートしてから削除する
ja.ExportWhere=バックアップの保存先 (中に VST-Ex-backup フォルダを作ります):
ja.OK=OK
ja.Strays=プラグインのフォルダに、プラグインが作ったものではないファイルがあります (別のファイルや、.bak / .org など名前を変えた複製)。削除するものにチェックしてください。それ以外は残します。
ja.DeleteTicked=チェックしたものを削除
ja.KeepAll=すべて残す
ja.Exported=バックアップを保存しました:%n%1
fr.TypeFull=Installation complète
fr.TypeCustom=Installation personnalisée
fr.PartCore=VST Ex Audio Filter (requis) : Filtres (VST Ex) dans le clic droit, filtres hauteur / pseudo-stéréo, Outils > VST Ex Audio Filter
fr.PartLoader=VST Enigmax Loader : charge les plugins VST2 / VST3 (analyse sûre, navigateur, captures des éditeurs, export / import des réglages)
fr.PartChainView=VST Ex Chain View : la chaîne VST de chaque source audio (périphériques, filtres, latence, niveaux) avec un imageur sonore. Menu Outils et bouton du dock
fr.PartMixer=Dock VST Ex Mixer : sous chaque vumètre du mixeur audio, un bouton par filtre VST de la source. Clic : ON/OFF, clic droit : ouvrir le plugin
fr.PartSwitcher=Dock VST Ex Switcher : boutons des ensembles ON/OFF, et utilisation CPU d'OBS avec la part prise par les plugins VST
fr.CloseObs=OBS est ouvert. Quittez OBS puis cliquez sur Réessayer.
fr.LoaderFiltersLost=Vos collections de scènes contiennent %1 filtre(s) VST Enigmax Loader. Sans VST Enigmax Loader, ils disparaissent d'OBS.%n%nPour les garder, déplacez-les d'abord vers OBS VST 2.x / atkAudio dans OBS : Outils > VST Enigmax Loader > Mode débogage > Transfert.%n%nArrêter maintenant pour le faire ?
fr.KeepTitle=Que faut-il supprimer ?
fr.KeepIntro=Choisissez ce qui est supprimé. Ce qui n'est pas coché reste sur ce PC.
fr.DelThumbs=Miniatures des plugins (basse résolution)
fr.DelShots=Captures haute résolution
fr.DelSettings=Réglages, préréglages de chaîne et historique des filtres
fr.ExportFirst=Tout exporter avant de supprimer
fr.ExportWhere=Dossier de la sauvegarde (un dossier VST-Ex-backup y est créé) :
fr.OK=OK
fr.Strays=Ces fichiers dans les dossiers du plugin n'ont pas été créés par lui (autres fichiers, ou copies renommées .bak / .org). Cochez ceux à supprimer ; les autres sont gardés.
fr.DeleteTicked=Supprimer les cochés
fr.KeepAll=Tout garder
fr.Exported=Sauvegarde enregistrée dans :%n%1
ko.TypeFull=전체 설치
ko.TypeCustom=사용자 지정 설치
ko.PartCore=VST Ex Audio Filter (필수): 우클릭 메뉴의 필터 (VST Ex), 피치 / 의사 스테레오 필터, 도구 > VST Ex Audio Filter
ko.PartLoader=VST Enigmax Loader: VST2 / VST3 플러그인 불러오기 (안전한 스캔, 플러그인 브라우저, 에디터 스크린샷, 설정 내보내기 / 가져오기)
ko.PartChainView=VST Ex Chain View: 모든 오디오 소스의 VST 체인 (장치, 필터, 지연, 레벨) 표시와 사운드 이미저. 도구 메뉴와 도크 버튼
ko.PartMixer=VST Ex Mixer 도크: 오디오 믹서의 각 미터 아래에 그 소스의 VST 필터마다 버튼. 클릭: ON/OFF, 우클릭: 플러그인 열기
ko.PartSwitcher=VST Ex Switcher 도크: ON/OFF 세트 버튼과, OBS CPU 사용률 중 VST 플러그인이 쓰는 비율 표시
ko.CloseObs=OBS가 실행 중입니다. OBS를 종료한 뒤 다시 시도를 누르세요.
ko.LoaderFiltersLost=장면 모음에 VST Enigmax Loader 필터가 %1개 있습니다. VST Enigmax Loader가 없으면 OBS에서 사라집니다.%n%n유지하려면 먼저 OBS의 도구 > VST Enigmax Loader > 디버그 모드 > 인계 에서 OBS VST 2.x / atkAudio로 옮기세요.%n%n지금 중단하고 옮길까요?
ko.KeepTitle=무엇을 삭제할까요?
ko.KeepIntro=삭제할 항목을 고르세요. 체크하지 않은 항목은 이 PC에 남습니다.
ko.DelThumbs=플러그인 썸네일 (저해상도)
ko.DelShots=고해상도 스크린샷
ko.DelSettings=설정, 체인 프리셋, 필터 기록
ko.ExportFirst=모두 내보낸 뒤 삭제
ko.ExportWhere=백업 저장 폴더 (안에 VST-Ex-backup 폴더를 만듭니다):
ko.OK=확인
ko.Strays=플러그인 폴더에 플러그인이 만들지 않은 파일이 있습니다 (다른 파일이나 .bak / .org 같은 이름을 바꾼 복사본). 삭제할 파일을 체크하세요. 나머지는 남겨 둡니다.
ko.DeleteTicked=체크한 파일 삭제
ko.KeepAll=모두 유지
ko.Exported=백업을 저장했습니다:%n%1

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "core"; Description: "{cm:PartCore}"; Types: full custom; Flags: fixed
Name: "loader"; Description: "{cm:PartLoader}"; Types: full
Name: "chainview"; Description: "{cm:PartChainView}"; Types: full
Name: "mixer"; Description: "{cm:PartMixer}"; Types: full
Name: "switcher"; Description: "{cm:PartSwitcher}"; Types: full

[InstallDelete]
; 1.0.0 had the scanner inside VST Ex's folder; it now belongs to VST Enigmax Loader.
Type: files; Name: "{app}\{#MainName}\bin\64bit\vst-ex-scanner.exe"
; Unticking the loader on a reinstall removes it (the warning comes on the components page).
Type: filesandordirs; Name: "{app}\{#LoaderName}"; Components: not loader

[Files]
Source: "{#StageDir}\{#MainName}\*"; DestDir: "{app}\{#MainName}"; Excludes: "*.pdb"; Flags: recursesubdirs ignoreversion; Components: core
Source: "{#StageDir}\{#LoaderName}\*"; DestDir: "{app}\{#LoaderName}"; Excludes: "*.pdb"; Flags: recursesubdirs ignoreversion; Components: loader

[Dirs]
Name: "{userappdata}\obs-studio\plugin_config\{#MainName}"

[INI]
Filename: "{#FeatureIni}"; Section: "features"; Key: "featureChainView"; String: "true"; Components: chainview
Filename: "{#FeatureIni}"; Section: "features"; Key: "featureChainView"; String: "false"; Components: not chainview
Filename: "{#FeatureIni}"; Section: "features"; Key: "featureMixer"; String: "true"; Components: mixer
Filename: "{#FeatureIni}"; Section: "features"; Key: "featureMixer"; String: "false"; Components: not mixer
Filename: "{#FeatureIni}"; Section: "features"; Key: "featureSwitcher"; String: "true"; Components: switcher
Filename: "{#FeatureIni}"; Section: "features"; Key: "featureSwitcher"; String: "false"; Components: not switcher

[Code]
var
  DelThumbs, DelShots, DelSettings, ExportFirst: Boolean;
  ExportDir: String;

{ ---------- OBS running ---------- }

function ObsRunning: Boolean;
var
  Locator, Service, Found: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Found := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name="obs64.exe" OR Name="obs32.exe"');
    Result := Found.Count > 0;
  except
  end;
end;

function WaitForObsClosed: Boolean;
begin
  Result := True;
  while ObsRunning do
    if MsgBox(CustomMessage('CloseObs'), mbError, MB_RETRYCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
end;

{ ---------- paths ---------- }

function ConfigDir(Name: String): String;
begin
  Result := ExpandConstant('{userappdata}\obs-studio\plugin_config\') + Name;
end;

{ "dataDir" from a module's plugin-meta.json (Qt writes one key per line, UTF-8, forward slashes). }
function DataDirOf(Name: String): String;
var
  Lines: TArrayOfString;
  I, P: Integer;
  S: String;
begin
  Result := ConfigDir(Name);
  if not LoadStringsFromFile(ConfigDir(Name) + '\plugin-meta.json', Lines) then
    Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    S := Trim(Lines[I]);
    if Pos('"dataDir": "', S) = 1 then
    begin
      S := Copy(S, Length('"dataDir": "') + 1, MaxInt);
      P := Pos('"', S);
      if P > 1 then
      begin
        S := Copy(S, 1, P - 1);
        StringChangeEx(S, '\\', '\', True);
        StringChangeEx(S, '/', '\', True);
        Result := S;
      end;
      Exit;
    end;
  end;
end;

{ ---------- VST Enigmax Loader filters still in use ---------- }

function CountIn(const Text, Pattern: String): Integer;
var
  S: String;
  P: Integer;
begin
  Result := 0;
  S := Text;
  P := Pos(Pattern, S);
  while P > 0 do
  begin
    Result := Result + 1;
    Delete(S, 1, P + Length(Pattern) - 1);
    P := Pos(Pattern, S);
  end;
end;

function CountLoaderFilters: Integer;
var
  Dir: String;
  Rec: TFindRec;
  Text: AnsiString;
begin
  Result := 0;
  Dir := ExpandConstant('{userappdata}\obs-studio\basic\scenes\');
  if FindFirst(Dir + '*.json', Rec) then
  try
    repeat
      if LoadStringFromFile(Dir + Rec.Name, Text) then
        Result := Result + CountIn(String(Text), '"id": "vst_ex_audio_filter"') +
                  CountIn(String(Text), '"id":"vst_ex_audio_filter"');
    until not FindNext(Rec);
  finally
    FindClose(Rec);
  end;
end;

{ True = go on. }
function ConfirmLoaderRemoval: Boolean;
var
  N: Integer;
begin
  Result := True;
  N := CountLoaderFilters;
  if N > 0 then
    Result := MsgBox(FmtMessage(CustomMessage('LoaderFiltersLost'), [IntToStr(N)]), mbConfirmation, MB_YESNO) = IDNO;
end;

{ ---------- setup ---------- }

function InitializeSetup: Boolean;
begin
  Result := WaitForObsClosed;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and not WizardIsComponentSelected('loader') and
     DirExists(ExpandConstant('{app}\{#LoaderName}')) then
    Result := ConfirmLoaderRemoval;
end;

{ ---------- uninstall: what to keep ---------- }

procedure AskWhatToDelete;
var
  Form: TSetupForm;
  Intro, WhereLabel: TNewStaticText;
  Thumbs, Shots, Settings, Exp: TNewCheckBox;
  Where: TNewEdit;
  Ok: TNewButton;
  Y: Integer;
begin
  Form := CreateCustomForm(ScaleX(460), ScaleY(250), False, False);
  try
    Form.Caption := CustomMessage('KeepTitle');
    Intro := TNewStaticText.Create(Form);
    Intro.Parent := Form;
    Intro.Left := ScaleX(12);
    Intro.Top := ScaleY(12);
    Intro.Width := Form.ClientWidth - ScaleX(24);
    Intro.AutoSize := False;
    Intro.WordWrap := True;
    Intro.Height := ScaleY(30);
    Intro.Caption := CustomMessage('KeepIntro');
    Y := ScaleY(48);
    Thumbs := TNewCheckBox.Create(Form);
    Shots := TNewCheckBox.Create(Form);
    Settings := TNewCheckBox.Create(Form);
    Exp := TNewCheckBox.Create(Form);
    Thumbs.Caption := CustomMessage('DelThumbs');
    Shots.Caption := CustomMessage('DelShots');
    Settings.Caption := CustomMessage('DelSettings');
    Exp.Caption := CustomMessage('ExportFirst');
    Thumbs.Parent := Form; Shots.Parent := Form; Settings.Parent := Form; Exp.Parent := Form;
    Thumbs.Left := ScaleX(12); Shots.Left := ScaleX(12); Settings.Left := ScaleX(12); Exp.Left := ScaleX(12);
    Thumbs.Width := Form.ClientWidth - ScaleX(24); Shots.Width := Thumbs.Width;
    Settings.Width := Thumbs.Width; Exp.Width := Thumbs.Width;
    Thumbs.Top := Y; Shots.Top := Y + ScaleY(24); Settings.Top := Y + ScaleY(48); Exp.Top := Y + ScaleY(80);
    WhereLabel := TNewStaticText.Create(Form);
    WhereLabel.Parent := Form;
    WhereLabel.Left := ScaleX(30);
    WhereLabel.Top := Y + ScaleY(104);
    WhereLabel.Caption := CustomMessage('ExportWhere');
    Where := TNewEdit.Create(Form);
    Where.Parent := Form;
    Where.Left := ScaleX(30);
    Where.Top := Y + ScaleY(122);
    Where.Width := Form.ClientWidth - ScaleX(42);
    Where.Text := ExpandConstant('{userdesktop}');
    Ok := TNewButton.Create(Form);
    Ok.Parent := Form;
    Ok.Caption := CustomMessage('OK');
    Ok.ModalResult := mrOk;
    Ok.Width := ScaleX(90);
    Ok.Height := ScaleY(25);
    Ok.Left := Form.ClientWidth - Ok.Width - ScaleX(12);
    Ok.Top := Form.ClientHeight - Ok.Height - ScaleY(10);
    Form.ActiveControl := Ok;
    Form.ShowModal;
    DelThumbs := Thumbs.Checked;
    DelShots := Shots.Checked;
    DelSettings := Settings.Checked;
    ExportFirst := Exp.Checked;
    ExportDir := Trim(Where.Text);
  finally
    Form.Free;
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := WaitForObsClosed and ConfirmLoaderRemoval;
  if Result and not UninstallSilent then
    AskWhatToDelete;
end;

{ ---------- uninstall: files ---------- }

function IsHex(const S: String; AllowDash: Boolean): Boolean;
var
  I: Integer;
begin
  Result := Length(S) > 0;
  for I := 1 to Length(S) do
    if not (((S[I] >= '0') and (S[I] <= '9')) or ((S[I] >= 'a') and (S[I] <= 'f')) or (AllowDash and (S[I] = '-'))) then
      Result := False;
end;

{ Our picture names: <40 hex sha1>.png or filter-<uuid>.png, with -full before .png in screenshots\. }
function IsOurPicture(Name: String; Full: Boolean): Boolean;
var
  Base: String;
begin
  Result := False;
  if Lowercase(ExtractFileExt(Name)) <> '.png' then
    Exit;
  Base := ChangeFileExt(Name, '');
  if Full then
  begin
    if Copy(Base, Length(Base) - 4, 5) <> '-full' then
      Exit;
    Base := Copy(Base, 1, Length(Base) - 5);
  end;
  if Copy(Base, 1, 7) = 'filter-' then
    Result := (Length(Base) = 7 + 36) and IsHex(Copy(Base, 8, 36), True)
  else
    Result := (Length(Base) = 40) and IsHex(Base, False);
end;

{ category: 1 thumbnails, 2 screenshots, 3 settings/history/presets, 0 not ours }
function CategoryOf(const Folder, Name: String): Integer;
var
  N: String;
begin
  Result := 0;
  N := Lowercase(Name);
  if Folder = 'thumbnails' then begin if IsOurPicture(N, False) then Result := 1; end
  else if Folder = 'screenshots' then begin if IsOurPicture(N, True) then Result := 2; end
  else if Folder = 'history' then begin
    if (ExtractFileExt(N) = '.json') and (Length(N) = 36 + 5) and IsHex(Copy(N, 1, 36), True) then Result := 3; end
  else if Folder = 'presets' then begin if N = 'chain-presets.json' then Result := 3; end
  else if Folder = 'config' then begin
    if (N = 'plugin-meta.json') or (N = 'plugin-cache.json') or (N = 'install-features.ini') then Result := 3; end;
end;

procedure CopyTree(const FromDir, ToDir: String; TopOnly: Boolean);
var
  Rec: TFindRec;
begin
  if FindFirst(FromDir + '\*', Rec) then
  try
    repeat
      if (Rec.Name <> '.') and (Rec.Name <> '..') then
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
        begin
          if not TopOnly then
            CopyTree(FromDir + '\' + Rec.Name, ToDir + '\' + Rec.Name, False);
        end
        else
        begin
          ForceDirectories(ToDir);
          FileCopy(FromDir + '\' + Rec.Name, ToDir + '\' + Rec.Name, False);
        end;
    until not FindNext(Rec);
  finally
    FindClose(Rec);
  end;
end;

procedure ExportAll;
var
  Dest: String;
  Ini: TArrayOfString;
begin
  Dest := ExportDir + '\VST-Ex-backup-' + GetDateTimeString('yyyymmdd-hhnnss', #0, #0);
  CopyTree(ConfigDir('{#MainName}'), Dest + '\config\{#MainName}', True);
  CopyTree(ConfigDir('{#LoaderName}'), Dest + '\config\{#LoaderName}', True);
  CopyTree(DataDirOf('{#LoaderName}') + '\thumbnails', Dest + '\data\thumbnails', False);
  CopyTree(DataDirOf('{#LoaderName}') + '\screenshots', Dest + '\data\screenshots', False);
  CopyTree(DataDirOf('{#MainName}') + '\history', Dest + '\data\history', False);
  CopyTree(DataDirOf('{#MainName}') + '\presets', Dest + '\data\presets', False);
  SetArrayLength(Ini, 2);
  Ini[0] := '[backup]';
  Ini[1] := 'version=1';
  SaveStringsToUTF8FileWithoutBOM(Dest + '\vst-ex-backup.ini', Ini, False);
  MsgBox(FmtMessage(CustomMessage('Exported'), [Dest]), mbInformation, MB_OK);
end;

var
  Strays: TStringList;

{ Deletes our files of the ticked categories in Dir; anything not ours goes on the stray list. }
procedure SweepFolder(const Dir, Folder: String; Recurse: Boolean);
var
  Rec: TFindRec;
  Cat: Integer;
begin
  if FindFirst(Dir + '\*', Rec) then
  try
    repeat
      if (Rec.Name = '.') or (Rec.Name = '..') then
        Continue;
      if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
      begin
        if Recurse and not ((Folder = 'plugin') and (Lowercase(Rec.Name) = 'uninstall')) then
          SweepFolder(Dir + '\' + Rec.Name, Folder, True);
        Continue;
      end;
      Cat := CategoryOf(Folder, Rec.Name);
      if Cat = 0 then
        Strays.Add(Dir + '\' + Rec.Name)
      else if ((Cat = 1) and DelThumbs) or ((Cat = 2) and DelShots) or ((Cat = 3) and DelSettings) then
        DeleteFile(Dir + '\' + Rec.Name);
    until not FindNext(Rec);
  finally
    FindClose(Rec);
  end;
end;

procedure AskAboutStrays;
var
  Form: TSetupForm;
  Text: TNewStaticText;
  List: TNewCheckListBox;
  Del, Keep: TNewButton;
  I: Integer;
begin
  if Strays.Count = 0 then
    Exit;
  Form := CreateCustomForm(ScaleX(560), ScaleY(340), True, True);
  try
    Form.Caption := CustomMessage('KeepTitle');
    Text := TNewStaticText.Create(Form);
    Text.Parent := Form;
    Text.Left := ScaleX(12);
    Text.Top := ScaleY(10);
    Text.Width := Form.ClientWidth - ScaleX(24);
    Text.Height := ScaleY(44);
    Text.AutoSize := False;
    Text.WordWrap := True;
    Text.Caption := CustomMessage('Strays');
    List := TNewCheckListBox.Create(Form);
    List.Parent := Form;
    List.Left := ScaleX(12);
    List.Top := ScaleY(58);
    List.Width := Form.ClientWidth - ScaleX(24);
    List.Height := Form.ClientHeight - ScaleY(58) - ScaleY(46);
    for I := 0 to Strays.Count - 1 do
      List.AddCheckBox(Strays[I], '', 0, False, True, False, False, nil);
    Del := TNewButton.Create(Form);
    Del.Parent := Form;
    Del.Caption := CustomMessage('DeleteTicked');
    Del.ModalResult := mrOk;
    Del.Width := ScaleX(160);
    Del.Height := ScaleY(25);
    Del.Top := Form.ClientHeight - Del.Height - ScaleY(10);
    Del.Left := Form.ClientWidth - ScaleX(12) - ScaleX(100) - ScaleX(8) - Del.Width;
    Keep := TNewButton.Create(Form);
    Keep.Parent := Form;
    Keep.Caption := CustomMessage('KeepAll');
    Keep.ModalResult := mrCancel;
    Keep.Width := ScaleX(100);
    Keep.Height := ScaleY(25);
    Keep.Top := Del.Top;
    Keep.Left := Form.ClientWidth - ScaleX(12) - Keep.Width;
    if Form.ShowModal = mrOk then
      for I := 0 to Strays.Count - 1 do
        if List.Checked[I] then
          DeleteFile(Strays[I]);
  finally
    Form.Free;
  end;
end;

procedure RemoveEmptyDirs(const Dir: String);
var
  Rec: TFindRec;
begin
  if FindFirst(Dir + '\*', Rec) then
  try
    repeat
      if (Rec.Name <> '.') and (Rec.Name <> '..') and ((Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) then
        RemoveEmptyDirs(Dir + '\' + Rec.Name);
    until not FindNext(Rec);
  finally
    FindClose(Rec);
  end;
  RemoveDir(Dir); { only succeeds when empty }
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Main, Loader, MainData, LoaderData: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  Main := ConfigDir('{#MainName}');
  Loader := ConfigDir('{#LoaderName}');
  MainData := DataDirOf('{#MainName}');
  LoaderData := DataDirOf('{#LoaderName}');
  if ExportFirst and (DelThumbs or DelShots or DelSettings) then
    ExportAll;
  Strays := TStringList.Create;
  try
    Strays.Duplicates := dupIgnore;
    Strays.Sorted := True;
    SweepFolder(Main, 'config', False);
    SweepFolder(Loader, 'config', False);
    SweepFolder(LoaderData + '\thumbnails', 'thumbnails', True);
    SweepFolder(LoaderData + '\screenshots', 'screenshots', True);
    SweepFolder(MainData + '\history', 'history', True);
    SweepFolder(MainData + '\presets', 'presets', True);
    { Whatever is still in the plugin folders after uninstall was not installed by us. }
    SweepFolder(ExpandConstant('{app}\{#MainName}'), 'plugin', True);
    SweepFolder(ExpandConstant('{app}\{#LoaderName}'), 'plugin', True);
    if not UninstallSilent then
      AskAboutStrays;
  finally
    Strays.Free;
  end;
  RemoveEmptyDirs(LoaderData + '\thumbnails');
  RemoveEmptyDirs(LoaderData + '\screenshots');
  RemoveEmptyDirs(MainData + '\history');
  RemoveEmptyDirs(MainData + '\presets');
  RemoveEmptyDirs(Main);
  RemoveEmptyDirs(Loader);
  RemoveEmptyDirs(ExpandConstant('{app}\{#MainName}'));
  RemoveEmptyDirs(ExpandConstant('{app}\{#LoaderName}'));
end;
