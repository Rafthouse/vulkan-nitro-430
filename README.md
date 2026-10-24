# Збірка VST3 / AU (Windows & macOS) та Git-деплой для VULKAN AUDIO NITRO-430

Цей проєкт повністю підготовлений як для **хмарної автоматичної збірки через GitHub Actions** (одночасно під Windows x64 та macOS Universal Binary без потреби мати два фізичні комп'ютери), так і для **локальної компіляції через CMake**.

## 1. Структура репозиторію

| Файл / Директорія | Призначення |
| :--- | :--- |
| `CMakeLists.txt` | Кросплатформний конфіг CMake (автоматично завантажує JUCE 8.0.4 через `FetchContent`, компілює VST3, AU та Standalone) |
| `src/ValleyPeopleDynaMite.cpp` | Повний C++17 / JUCE рушій (TA-101 VCA, Linear Integration, 3-Zone Range, ARC, скін та вікно About за кліком на назву) |
| `web/valley_people_dynamite.html` | Інтерактивний Web Audio API + Canvas прототип для браузера |
| `.github/workflows/build.yml` | CI/CD пайплайн GitHub Actions, який при кожному `git push` збирає інсталяційні бандли `.vst3`, `.component` (AU) та `.exe`/`.app` |

## 2. Як вписати своє ім'я автора перед комітом

Ти можеш задати своє ім'я автора, бренд та назву моделі одним із двох способів:

1. **Прямо у `CMakeLists.txt` (рядки 19–21)** або через прапорці CMake:
   ```bash
   cmake -B build -DPLUGIN_AUTHOR_NAME="Твоє Ім'я" -DPLUGIN_BRAND_TITLE="VULKAN AUDIO" -DPLUGIN_MODEL_TITLE="NITRO-430"
   ```
2. **У файлі `ValleyPeopleDynaMite.cpp` (рядки 27–38)**:
   ```cpp
   #define PLUGIN_BRAND_TITLE  "VULKAN AUDIO"
   #define PLUGIN_MODEL_TITLE  "NITRO-430"
   #define PLUGIN_AUTHOR_NAME  "Твоє Ім'я / Нікнейм"
   ```

## 3. Швидкий Git-коміт і автоматична збірка Windows + Mac у GitHub Actions

Якщо розпакувати готовий архів `VulkanAudio_NITRO430_GitRepo.zip` (у ньому вже ініціалізовано `.git` і зроблено перший коміт) або покласти файли в папку проєкту, достатньо запушити їх у свій GitHub-репозиторій:

```bash
# Якщо створюєш репозиторій з нуля у своїй папці:
git init -b main
git add .
git commit -m "feat: initial release of Vulkan Audio NITRO-430 (VST3/AU)"

# Прив'язка до твого репозиторію на GitHub та пуш:
git remote add origin https://github.com/<твій-юзернейм>/vulkan-nitro-430.git
git push -u origin main
```

Одразу після `git push` перейди у вкладку **Actions** свого репозиторію на GitHub:
* Запустяться дві паралельні матриці: **Windows x64 (VST3 + Standalone)** та **macOS Universal (VST3 + AU + Standalone)**.
* Через ~3–5 хвилин унизу сторінки білду з'являться два готові архіви в секції **Artifacts**:
  * `VulkanAudio-NITRO-430-Windows-x64-VST3.zip` (містить `.vst3` та `.exe`)
  * `VulkanAudio-NITRO-430-macOS-Universal-VST3-AU.zip` (містить Universal Binary `.vst3`, AudioUnit `.component` та `.app`, вже підписані ad-hoc підписом для Apple Silicon M1–M4).

## 4. Локальна збірка на Windows (VST3 + Standalone)

### Необхідні інструменти
* **Visual Studio 2022** (Community або вище) з компонентом **Desktop development with C++**
* **CMake 3.22+** та **Git**

### Команди збірки (PowerShell або Developer Command Prompt)
```powershell
# 1. Конфігурація проєкту (CMake сам завантажить JUCE 8.0.4)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# 2. Компіляція у режимі Release
cmake --build build --config Release --parallel
```

### Де лежать готові плагіни на Windows та куди їх ставити
* **Зібраний VST3**: `build/VulkanNitro430_artefacts/Release/VST3/Vulkan Audio NITRO-430.vst3`
* **Системна папка VST3 для DAW (Reaper, Ableton, Cubase, FL Studio)**:
  ```text
  C:\Program Files\Common Files\VST3\
  ```

## 5. Локальна збірка на macOS (VST3 + AudioUnit AU + Standalone)

### Необхідні інструменти
* **Xcode** або **Xcode Command Line Tools** (`xcode-select --install`)
* **CMake 3.22+** (`brew install cmake`) та **Git**

### Команди збірки (Terminal)
```bash
# 1. Конфігурація Universal Binary (Apple Silicon arm64 + Intel x86_64)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

# 2. Компіляція VST3, AU та Standalone
cmake --build build --config Release --parallel

# 3. Локальний ad-hoc підпис (обов'язково для macOS на чіпах M1/M2/M3/M4)
codesign --force --deep --sign - "build/VulkanNitro430_artefacts/Release/VST3/Vulkan Audio NITRO-430.vst3"
codesign --force --deep --sign - "build/VulkanNitro430_artefacts/Release/AU/Vulkan Audio NITRO-430.component"
```

### Куди скопіювати плагіни на macOS
```bash
# Встановлення VST3 (для Ableton, Reaper, Cubase, Studio One):
cp -R "build/VulkanNitro430_artefacts/Release/VST3/Vulkan Audio NITRO-430.vst3" ~/Library/Audio/Plug-Ins/VST3/

# Встановлення AudioUnit AU (для Logic Pro, GarageBand, Ableton):
cp -R "build/VulkanNitro430_artefacts/Release/AU/Vulkan Audio NITRO-430.component" ~/Library/Audio/Plug-Ins/Components/

# Перевірка валідатором Apple AudioUnit (auval):
auval -v aufx N430 Vulk
```
