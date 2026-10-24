# Vulkan Audio NITRO-430

**Компактний VCA-динамічний процесор (компресор / лімітер / гейт)** у форматі 1U half-rack — апаратно точна модель TA-101 VCA. Формати: **VST3, AU (AudioUnit), Standalone** для Windows та macOS (Universal Binary: Apple Silicon M1–M4 + Intel).

Автор: **Mykyta Shchur**

---

## ⬇️ Завантажити та встановити

Оберіть файл під свою систему:

### Windows (VST3 + Standalone)

| Файл | Опис |
|---|---|
| [VulkanAudio-NITRO-430-Windows-Setup-x64.exe](https://github.com/Rafthouse/vulkan-nitro-430/releases/download/v1.0.0/VulkanAudio-NITRO-430-Windows-Setup-x64.exe) | Інсталятор — ставить VST3 у систему + Standalone-застосунок |

1. Скачайте `.exe` і запустіть його.
2. Інсталятор сам покладе VST3 у `C:\Program Files\Common Files\VST3\` і Standalone у `Program Files\Vulkan Audio\NITRO-430\`.
3. Перезапустіть DAW (Reaper, Ableton, Cubase, FL Studio) — плагін з'явиться у списку VST3.

### macOS (VST3 + AU + Standalone)

| Файл | Опис |
|---|---|
| [VulkanAudio-NITRO-430-macOS-Universal2.pkg](https://github.com/Rafthouse/vulkan-nitro-430/releases/download/v1.0.0/VulkanAudio-NITRO-430-macOS-Universal2.pkg) | Інсталятор — ставить VST3 + AU у системні папки |
| [VulkanAudio-NITRO-430-macOS-Universal2.dmg](https://github.com/Rafthouse/vulkan-nitro-430/releases/download/v1.0.0/VulkanAudio-NITRO-430-macOS-Universal2.dmg) | Disk image — перетягніть плагіни вручну |

1. Скачайте `.pkg` (найпростіше) або `.dmg`.
2. Для `.pkg`: двічі клацніть → Installer → далі.
3. Для `.dmg`: відкрийте образ і перетягніть `.vst3` у `~/Library/Audio/Plug-Ins/VST3/`, а `.component` у `~/Library/Audio/Plug-Ins/Components/`.
4. Перезапустіть DAW (Logic Pro, Ableton, Reaper, GarageBand).

> **Перший запуск на macOS:** збірки підписані ad-hoc (без Apple Developer ID). Якщо Gatekeeper скаржиться — правий клік на файлі → **Відкрити**, або в терміналі: `xattr -cr /шлях/до/файлу`.

---

## 🎛️ Як користуватися (параметри)

| Параметр | Діапазон | Що робить |
|---|---|---|
| **Threshold** | −40…+20 dB | Поріг спрацювання. Нижче = більше компресії |
| **Release** | 0.05…5 s | Час відновлення після стиснення |
| **Range** | 0…60 dB | Максимальна глибина ослаблення (для гейта — скільки зрізає) |
| **Output** | −15…+15 dB | Вихідний рівень (make-up gain) |
| **Detector Source** | INT / DS-FM / EXT | Джерело детектора: внутрішній, DS-FM (Dyna-Mite), зовнішній sidechain |
| **Mode** | EXP / OUT / LIMIT | Режим: експандер, компресор-лімітер, жорсткий лімітер |
| **Detector Type** | GATE / PEAK / AVG | Тип детектора: гейт, піковий, середній (RMS) |
| **Dry / Wet Mix** | 0…100% | Баланс сухого/обробленого сигналу (паралельна компресія) |
| **Stereo Link** | ON/OFF | Зв'язування каналів у стерео |

### Швидкі рецепти

- **Паралельна компресія на барабани:** Mode = LIMIT, Threshold ≈ −20 dB, Range ≈ 20 dB, Dry/Wet Mix ≈ 50%.
- **Гейт для гітари/вокалу:** Mode = EXP, Detector Type = GATE, Threshold ≈ −40 dB, Range = 60 dB.
- **М'яке вирівнювання майстра:** Mode = LIMIT, Detector Type = AVG, Release ≈ 0.5 s, Output для компенсації.

---

## 📦 Формати та вимоги

- **Windows:** VST3 (x64) + Standalone, Windows 10/11.
- **macOS:** VST3 + AU + Standalone, Universal Binary (arm64 + x86_64), macOS 11+.
- **DAW:** будь-які з підтримкою VST3/AU — Reaper, Ableton Live, Logic Pro, Cubase, Studio One, FL Studio, GarageBand.

---

## 🔧 Збірка з вихідного коду

Повна інструкція зі збірки Windows/macOS та інсталяторів — у [BUILD.md](BUILD.md).
