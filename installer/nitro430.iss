; Vulkan Audio NITRO-430 Windows Installer Script (Inno Setup)
#define MyAppName "Vulkan Audio NITRO-430"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "VULKAN AUDIO"
#define MyAppURL "https://github.com/Rafthouse/vulkan-nitro-430"

[Setup]
AppId={{8F2B1D3A-6C4E-4A1B-9D7E-2F5A8C1B0D3E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Vulkan Audio\NITRO-430
DisableDirPage=yes
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=VulkanAudio-NITRO-430-Windows-Setup-x64
OutputDir=..\dist-windows
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; VST3 bundle into the standard Windows VST3 directory (C:\Program Files\Common Files\VST3\)
Source: "..\build\VulkanNitro430_artefacts\Release\VST3\Vulkan Audio NITRO-430.vst3\*"; DestDir: "{commoncf64}\VST3\Vulkan Audio NITRO-430.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Standalone application
Source: "..\build\VulkanNitro430_artefacts\Release\Standalone\Vulkan Audio NITRO-430.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\Vulkan Audio NITRO-430.exe"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nVulkan Audio NITRO-430 is a compact VCA dynamics processor (VST3 + Standalone) by Mykyta Shchur.
