# iUnlocker GLTool

## Critical

**Do NOT put the game on Magisk/KSU DenyList.**  
DenyList prevents Zygisk from injecting into that process — hooks never run.

Enable Zygisk. Flash module. Save package in WebUI. Force-stop game. Relaunch.

## Debug

```
adb logcat -s iunlocker
```

Expect lines: `target <pkg>`, `plt …`, `pltHookCommit=1`.  
If you see `on DenyList`, remove the package from DenyList.

## Config

`/data/adb/iunlocker/config.json` — per-package map from WebUI.
