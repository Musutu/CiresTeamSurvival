# Local Unreal environment

Inspected September 23, 2026. Synced project reference files were not changed.

## Unreal Engine

- Installed engine: `F:/UE_5.8`, version **5.8.3**, changelist 58210709.
- Engine and Fab module BuildId both equal `55116800` (the compatible changelist).
- Existing prototype: `C:/Users/Eric/Documents/Unreal Projects/CireTeamSurvival`.
- The existing prototype's newest log is from August 6 and used UE 5.8.1. Its old network warnings do not explain today's failure.

## Confirmed Fab startup failure

Today's engine log is `C:/Users/Eric/AppData/Local/UnrealEngine/5.8/Saved/Logs/Unreal.log`.
At line 1615, the September 23 08:13:09 local startup reports failure to load
`F:/UE_5.8/Engine/Plugins/Fab/Binaries/Win64/UnrealEditor-Fab.dll` with Windows error **4551**.
Windows resolves that code to **“An Application Control policy has blocked this file.”**

The `Microsoft-Windows-CodeIntegrity/Operational` event log independently confirms this:

- Events **3033 and 3077**, at 08:09:52, 08:10:26, and 08:13:09 local time, name that exact DLL.
- Enforced policy ID: `{0283ac0f-fff1-49ae-ada1-8a933130cad6}`.
- Event XML policy name: `VerifiedAndReputableDesktop`, status `0xc0e90002` (matching the displayed Bad Image dialog).
- Windows reports unmet signing requirements or violation of Code Integrity policy.
- The DLL's Authenticode status is `NotSigned`.
- File SHA256: `C143668F276DAFE08A51904873C6F459B8EFDA63669F12431B6FB5690D0D27A8`.
- `VerifiedAndReputablePolicyState` is `1`, consistent with Smart App Control being on.

Fab exists, is enabled by default, and has matching module metadata. This failure happens before Fab's embedded web page can open. Clearing browser caches or re-signing in will not address this recorded startup block. Epic's status page reported Fab operational when checked.

The appropriate repair path is an official updated/repaired Fab binary accepted by Windows, or review through the device administrator/Epic/Microsoft support. Epic Games Launcher **Library > UE 5.8 > menu > Verify** can repair altered/missing engine files, but verification alone cannot guarantee that Windows will accept an unchanged unsigned DLL. Do not remove Code Integrity policies, disable security controls, add antivirus exclusions, or forge a signature to force it to load. No such changes were made.

Development can proceed in a project that does not load the optional Fab plugin, with assets obtained through the ordinary Fab website/launcher. This leaves the blocked binary unloaded; it does not fix the plugin. Re-enable Fab only after the official plugin is accepted by Windows.

Sources: [Fab in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/fab-window-in-unreal-engine), [Microsoft Smart App Control event diagnostics](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/test-your-app-with-smart-app-control), [Epic service status](https://status.epicgames.com/).

## C++ build prerequisites

Initial inspection found no installed MSVC compiler or Visual Studio Build Tools. The user subsequently approved the prepared Microsoft installer. It completed with exit **0** and no restart required. `vswhere` now reports Build Tools **17.14.41**, complete and launchable.

Verified installed compiler: `C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe`. Its **actual product version is 14.44.35229.0**, supported by this engine. Microsoft servicing preserves the older folder name; the folder name alone must not be treated as the compiler patch version.

Verified SDK library: `C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/x64/kernel32.lib`. The valid SDK registry root is under `WOW6432Node`; the unrelated 64-bit registry entry points to a missing directory.

The installed engine's `Engine/Config/Windows/Windows_SDK.json` specifies:

- Preferred Windows SDK: **10.0.22621.0**, minimum 10.0.19041.0.
- Supported preferred MSVC families: **14.44.35211 or newer within 14.44**, or 14.50.35723 or newer within 14.50.
- The engine bans 14.39 through 14.43 and the early 14.44/14.50 compiler releases.

`Tools/SetupBuildTools.ps1` prepares Microsoft **Visual Studio 2022 Build Tools 17.14.41**, MSVC v143 **14.44**, and Windows SDK **10.0.22621.0**. The Microsoft download URL and SHA256 were obtained from the official winget package metadata. Downloaded bootstrapper signature is **Valid**, signed by **Microsoft Corporation**, and SHA256 is **37BB0FB429D163ECEBD272A865D11A37B906D152BEF960DA2DDB29C2E2FD6EEB**.

Prepared installer: `C:/Users/Eric/AppData/Local/Temp/CiresTeamSurvival-BuildTools/vs_BuildTools-17.14.41.exe`.
Status file: `C:/Users/Eric/AppData/Local/Temp/CiresTeamSurvival-BuildTools/setup-status.json`.

Installation is **complete** after the user's ordinary Windows administrator approval. The setup script checks for admin rights and stops rather than trying to bypass UAC. The instructions below are retained for reproducible setup on a clean machine; there is no need to reinstall on this PC.

The first native Unreal build then identified one additional prerequisite: **.NET Framework SDK 4.6 or newer**, required by the engine's `SwarmInterface.Build.cs`. The initial minimal C++ install did not include it. Both setup helpers now also request `Microsoft.Net.Component.4.8.SDK` and `Microsoft.Net.Component.4.8.TargetingPack`. The user approved the repair, and the SDK is now verified at `C:/Program Files (x86)/Windows Kits/NETFXSDK/4.8`, including `Include/um/mscoree.h` and the expected `HKLM/SOFTWARE/WOW6432Node/Microsoft/Microsoft SDKs/NETFXSDK/4.8` registration. `Tools/RepairUnrealDotNet.cmd` remains available to reproduce this **modify** operation; it preserves existing components and prohibits automatic restart. MSBuild is already provided by the installed C++ workload; Unreal's C# build tool uses the engine's bundled .NET runtime. No additional NuGet workload is necessary for this specific missing native SDK.

To finish, double-click **`Tools/SetupBuildTools.cmd`** and approve the ordinary Windows administrator prompt. The wrapper verifies the downloaded Microsoft installer's signature/hash and starts that signed installer directly through normal Windows elevation. It records the installer return code in the status file. Afterward, ask Codex to verify the compiler and build the game.

The machine's Windows PowerShell 5.1 blocks script files. The wrapper does not change that setting or execute a `.ps1` file. The optional setup script is available for an administrator's PowerShell environment that already permits local scripts (its prepare mode was verified in the current PowerShell 7 environment):

```powershell
& 'C:\Users\Eric\.codex\.chatgpt-projects\g-p-6a5429b0ab4c81918c1025efbf9d6953\CiresTeamSurvival\Tools\SetupBuildTools.ps1'
```

The script checks the official installer hash/signature, installs only the required build components and dependencies, prevents automatic restart, and verifies the resulting compiler/SDK paths. Visual Studio installer diagnostics are written to the usual `%TEMP%/dd_*` logs. No PowerShell execution-policy or Windows security changes are required by this script.

Smart App Control can also affect newly built unsigned game binaries. After the compiler is installed, the new game's actual editor module and packaged executable must be built and launched to determine whether they are accepted. Disabling the optional Fab plugin in a project does not guarantee that all future game modules will load. If another file is blocked, retain the exact Code Integrity diagnostic and use the supported administrator/publisher review path.

Sources: [Microsoft command-line installation](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022), [Build Tools component IDs](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2022).

## Generated prototype content

`Tools/BuildContent.py` runs UnrealEditor-Cmd invisibly against the isolated, Blueprint-only `Tools/ContentBuilder/ContentBuilder.uproject`, with Fab disabled. The commandlet completed successfully using UE **5.8.3**, saved a blank `/Game/Maps/Citadel` map and six materials, and copied them into this project's `Content` directory. Material palette: charcoal stone, weathered bronze, teal emissive, rust-red emissive, gold, and slate.

The full installed Unreal template `Templates/TemplateResources/High/Characters/Content` was copied to `Content/Characters`, preserving all **128 files** and their references. It includes `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple` and `/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed`.

Verification records: `Tools/ContentBuilder/Saved/ContentBuild.json`, `ContentCopied.json`, and `Logs/ContentBuild-commandlet.log`. The commandlet exited **0** and logged `Python script executed successfully`. This validates asset generation, not a rendered scene or game-module launch.

## Medieval town content (September 24)

Third-party environment content now comes only from Poly Haven (CC0 1.0), downloaded with plain HTTPS
requests by `Tools/FetchTownAssets.py`; nothing downloaded is executed and Fab remains unused (its plugin is
still blocked by Windows Application Control). Imports run through an unattended UnrealEditor-Cmd Python
commandlet (`Tools/ImportTownContent.py`) against this project and write only `/Game/Environment/Town`.
Provenance: `Art/Environment/Town/PROVENANCE.md`. Layout, slots and runtime: `Docs/EnvironmentProps.md`.
Route and leak-zone details: `Docs/BattlefieldRoutes.md`.
