# iUnlocker GLTool — multi-game

No default package. Config starts empty; pick games in WebUI.

## Config

`/data/adb/iunlocker/config.json`

```json
{
  "games": {
    "com.example.game": {
      "enabled": true,
      "renderer": "Adreno (TM) 740",
      "vendor": "Qualcomm",
      "version": "OpenGL ES 3.2",
      "shading": "OpenGL ES GLSL ES 3.20",
      "ram": 8,
      "props": true,
      "paths": true
    }
  }
}
```

## Zygisk

- Place compiled `arm64-v8a.so` / `armeabi-v7a.so` under `zygisk/`
- Add each target package to Magisk DenyList
- Scan / pick / save in WebUI, then relaunch the game

## Hooks (per enabled package)

- open / openat path denial
- glGetString / eglQueryString GPU spoof
- /proc/self/maps line filter
- /proc/meminfo MemTotal rewrite
- property spoof on postAppSpecialize
