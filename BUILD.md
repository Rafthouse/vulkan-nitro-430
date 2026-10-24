# Збірка з вихідного коду

Повністю автоматична збірка через GitHub Actions (Windows x64 + macOS Universal) або локальна через CMake. JUCE 8.0.4 завантажується автоматично.

## Автоматично (GitHub Actions)

1. Клонуй репо та запхай у свій GitHub.
2. При кожному `git push` у `main` воркфлоу збирає обидві платформи, пакує інсталятори (Inno Setup `.exe` + `.pkg`/`.dmg`) і викладає артефакти.
3. По тегу `v*` артефакти потрапляють у Release.

## Локально — Windows (Visual Studio 2022 + CMake)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Готовий VST3: `build/VulkanNitro430_artefacts/Release/VST3/Vulkan Audio NITRO-430.vst3`.

## Локально — macOS (Xcode CLT + CMake)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --parallel
codesign --force --deep --sign - "build/VulkanNitro430_artefacts/Release/VST3/Vulkan Audio NITRO-430.vst3"
codesign --force --deep --sign - "build/VulkanNitro430_artefacts/Release/AU/Vulkan Audio NITRO-430.component"
```

## Інсталятори

- **Windows:** скрипт `installer/nitro430.iss` (Inno Setup) → `.exe`.
- **macOS:** у воркфлоу — `pkgbuild` (`.pkg`) та `create-dmg` (`.dmg`).

## Ім'я автора / бренд / модель

Змінюються у `CMakeLists.txt` (рядки `PLUGIN_AUTHOR_NAME`, `PLUGIN_BRAND_TITLE`, `PLUGIN_MODEL_TITLE`) або у `src/ValleyPeopleDynaMite.cpp` (макроси на початку файлу).
