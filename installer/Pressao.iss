; Windows installer for PRESSÃO (VST3).
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef DistDir
  #define DistDir "..\dist"
#endif

[Setup]
AppId={{6F3E2A41-9C0B-4E57-8D7A-2B1F5C9E4A13}
AppName=Pressao
AppVersion={#AppVersion}
AppPublisher=Audio Makers
DefaultDirName={autopf}\Audio Makers\Pressao
DisableProgramGroupPage=yes
DisableDirPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Pressao {#AppVersion} Instalador
OutputDir=..\dist
SetupIconFile=..\build\Pressao_artefacts\JuceLibraryCode\icon.ico
UninstallDisplayName=Pressao
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Files]
Source: "{#DistDir}\Pressao.vst3\*"; DestDir: "{code:Vst3Dir}\Pressao.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\Assets\Fonts\FONTS-LICENSE.txt"; DestDir: "{app}"; DestName: "Font licenses.txt"; Flags: ignoreversion

[Registry]
Root: HKLM; Subkey: "Software\Audio Makers\Pressao"; ValueType: string; ValueName: "VST3Path"; ValueData: "{code:Vst3Dir}"; Flags: uninsdeletekey

[UninstallDelete]
Type: filesandordirs; Name: "{reg:HKLM\Software\Audio Makers\Pressao,VST3Path|{commoncf64}\VST3}\Pressao.vst3"

[Code]
var
  Vst3Page: TInputDirWizardPage;

procedure InitializeWizard;
begin
  Vst3Page := CreateInputDirPage(wpWelcome, 'Pasta do VST3',
    'Onde o plugin VST3 vai ser instalado?',
    'O plugin vai para a pasta abaixo. A padr' + #227 + 'o ' + #233 + ' a pasta VST3 que todas as DAWs procuram. Clique em Procurar para escolher outra.',
    False, '');
  Vst3Page.Add('');
  Vst3Page.Values[0] := GetPreviousData('VST3Path', ExpandConstant('{commoncf64}\VST3'));
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'VST3Path', Vst3Page.Values[0]);
end;

function Vst3Dir(Param: String): String;
begin
  Result := Vst3Page.Values[0];
end;
