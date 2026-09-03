; Sillage Windows installer (Inno Setup 6).
;
; Build with:   iscc /DAppVersion=1.0.0 /DBuildDir=..\..\build\Sillage_artefacts\Release Sillage.iss
;
; Installs the VST3 into the shared 64-bit VST3 folder and (optionally) the
; standalone app into Program Files.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\Sillage_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6C1F1B62-3D9E-4B7C-9A1E-5B2E7D4F8A10}
AppName=Sillage
AppVersion={#AppVersion}
AppVerName=Sillage {#AppVersion}
AppPublisher=Elan Vital Studios
AppPublisherURL=https://elanvitalstudios.com
DefaultDirName={autopf}\Elan Vital Studios\Sillage
DefaultGroupName=Elan Vital Studios
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Sillage-{#AppVersion}-windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayName=Sillage
WizardStyle=modern

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#BuildDir}\VST3\Sillage.vst3\*"; DestDir: "{commoncf64}\VST3\Sillage.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#BuildDir}\Standalone\Sillage.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\Sillage"; Filename: "{app}\Sillage.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Sillage.vst3"
