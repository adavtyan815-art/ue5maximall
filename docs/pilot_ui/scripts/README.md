# Скрипты пилота UI (30.09.2026)

Воспроизводят перестилизацию `BP_Online_Offline_Selection_UI` и `BP_UserLogin_UI` по `../TZ_UMG_pilot_menu_login.md`.

| Файл | Что делает |
|---|---|
| `apply_ui_pilot.py` | Импортирует шрифты и текстуру, собирает `F_MaxiMall`, перестилизует оба виджета, компилирует, сохраняет, рендерит PNG для сверки |
| `run_apply.py` | Запускает `apply_ui_pilot.py` в редакторе и всегда закрывает редактор после |
| `inspect_widgets.py` | Только чтение: дерево виджетов (порядок, слоты, шрифты, стили) и T3D-экспорт графов в `out/` |
| `make_glow.py` | Генерирует `T_UI_RadialGlow.png` (Python 3 без пакетов) |
| `fonts/` | Instrument Sans и Inter 400/500/600, статические TTF с Google Fonts, лицензия SIL OFL 1.1. **В репозитории папки нет** (публикуются только скрипты и ТЗ): положить сюда шесть файлов `InstrumentSans-{Regular,Medium,SemiBold}.ttf` и `Inter-{Regular,Medium,SemiBold}.ttf`, скачанных с fonts.google.com |
| `T_UI_RadialGlow.png` | Генерируется `make_glow.py` |

## Нужно в проекте

- C++ `Source/awsTutorial/MaxiUI/` собран в `awsTutorialEditor` (скрипт вызывает `UMaxiUiDesignTools` и создаёт `UMaxiVisibilityToggleButton`).
- Python включается флагом `-EnablePlugins=PythonScriptPlugin`, `.uproject` не меняется.

## Запуск

Скрипт рассчитан на **исходные** ассеты. Имена новых виджетов `Maxi_*` уникальны, поэтому на уже перестилизованных ассетах он остановится с ошибкой «name already used» и ничего не сохранит.

1. Закрыть редактор.
2. Вернуть исходные `.uasset` из резервной копии.
3. Запустить (редактор откроется, выполнит скрипт и закроется):

```
UnrealEditor.exe "D:\awsTemplate_GameLift\awsTutorial.uproject" -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="<путь>/run_apply.py" -unattended -nop4 -nosplash
```

4. Результат — в `out/apply_log.txt` и `out/render_*.png`.

Почему не commandlet (`-run=pythonscript`): импорт шрифта обращается к `FSlateApplication`, а в commandlet Slate нет, и процесс падает на assert. Для `inspect_widgets.py` commandlet подходит:

```
UnrealEditor-Cmd.exe "D:\awsTemplate_GameLift\awsTutorial.uproject" -EnablePlugins=PythonScriptPlugin -run=pythonscript -script="<путь>/inspect_widgets.py" -unattended -nop4 -nosplash -nullrhi
```

## Что учтено в скрипте

- Существующие виджеты сохраняют имена: граф BP (46 привязанных событий) и 12 анимаций входа не меняются.
- Новые виджеты получают GUID и снятый флаг «Is Variable» (`RegisterMissingWidgetGuids`), поэтому компиляция проходит без `ensure`.
- Недоступное из Python выполняется через C++: размер кисти (`SetBrushImageSize`), составной шрифт (`SetupCompositeFont`). `SetDesiredSizeOverride` в ассет не сохраняется.
- У старых кнопок сброшен цветной `ColorAndOpacity`.
- Офлайн-рендер смешивает полупрозрачные слои в линейном пространстве, поэтому стекло на PNG светлее, чем в игре. Окончательная сверка цвета — в PIE.
