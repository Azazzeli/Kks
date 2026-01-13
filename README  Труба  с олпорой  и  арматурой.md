# HelloNRX - Полное описание кода

## Обзор

`HelloNRX.cpp` - это плагин для nanoCAD/Model Studio, который автоматически создает тестовую круглую трубу с опорой и арматурой (inline). Проект написан на C++ с использованием ObjectARX SDK и ViperCS SDK (Model Studio).

## Структура проекта

### Основные компоненты

1. **Точка входа** - `ncrxEntryPoint()` - стандартная точка входа для nanoCAD плагинов
2. **Главная функция** - `createTestPipe()` - автоматическое создание трубы с элементами
3. **Вспомогательные функции** - утилиты для логирования и форматирования

## Детальное описание кода

### Заголовочные файлы и настройки

```cpp
#define _ARXVER_202X
#define _ACRX_VER
#define NCAD            // nanoCAD
#define _MSC_VER 1920   // Visual Studio 2019
```

**Назначение**: Определение версии SDK и компилятора для правильной компиляции плагина.

#### Включаемые файлы

**Стандартные библиотеки:**
- `windows.h` - Windows API
- `tchar.h` - Unicode/TCHAR поддержка
- `<fstream>`, `<sstream>`, `<string>` - потоки и строки

**ObjectARX/nanoCAD SDK:**
- `acdb.h` - AutoCAD Database
- `dbmain.h` - Database main objects
- `dbapserv.h` - Database application services
- `dbtable.h` - Database tables (BlockTable, LayerTable, etc.)
- `dbents.h` - Database entities
- `geassign.h` - Geometry assignments
- `rxregsvc.h` - ObjectARX runtime services
- `acgi.h` - Graphics interface
- `aced.h` - AutoCAD Editor

**ViperCS/Model Studio SDK:**
- `vCSCreatePipe.h` - Создание труб
- `vCSProfileCircle.h` - Круглые профили труб
- `vCSProfileBase.h` - Базовый класс профилей
- `vCSDragManager.h` - Drag Manager для управления CAD операциями
- `vCSNode.h` - Узлы трубопроводной системы
- `vCSAxis.h` - Оси труб
- `vCSSegment.h` - Сегменты труб
- `vCSInLine.h` - Арматура (inline элементы)
- `vCSSupport.h` - Опоры
- `vCS_DM_Axis.h` - Drag Manager представление оси
- `vCS_DM_Seg.h` - Drag Manager представление сегмента
- `vCS_DM_ILBase.h` - Базовый класс для inline элементов в DM
- `VCSUtils.h` - Утилиты ViperCS

### Вспомогательные функции (namespace)

#### `GetLogFilePath()`

```cpp
std::wstring GetLogFilePath()
```

**Назначение**: Получает путь к лог-файлу в директории `%TEMP%`.

**Алгоритм:**
1. Использует Windows API `GetTempPathW()` для получения временной директории
2. Добавляет имя файла `HelloNRX.log`
3. Возвращает полный путь: `%TEMP%\HelloNRX.log`

**Возвращаемое значение**: `std::wstring` - путь к лог-файлу

---

#### `LogMessage()`

```cpp
void LogMessage(const wchar_t* fmt, ...)
```

**Назначение**: Универсальная функция логирования с поддержкой форматирования (как `printf`).

**Параметры:**
- `fmt` - форматная строка (wide string)
- `...` - переменное количество аргументов для форматирования

**Функциональность:**
1. Форматирует сообщение с использованием `_vsnwprintf_s()`
2. Добавляет временную метку в формате `[YYYY-MM-DD HH:MM:SS.mmm]`
3. Записывает в файл `%TEMP%\HelloNRX.log` (добавлением, не перезаписью)
4. Выводит в отладочный вывод через `OutputDebugStringW()` (видно в Visual Studio Debug Output)

**Пример использования:**
```cpp
LogMessage(L"Pipe created, axis ID: %ld", pipeAxisId.asOldId());
```

---

#### `PointToStr()`

```cpp
std::wstring PointToStr(const AcGePoint3d& pt)
```

**Назначение**: Преобразует 3D точку (`AcGePoint3d`) в строковое представление для логирования.

**Параметры:**
- `pt` - точка типа `AcGePoint3d` (x, y, z)

**Возвращаемое значение**: `std::wstring` в формате `"(x,y,z)"`

**Пример:**
```cpp
AcGePoint3d pt(100.0, 200.0, 50.0);
std::wstring str = PointToStr(pt); // "(100,200,50)"
```

---

### Главная функция: `createTestPipe()`

Это основная функция, которая автоматически создает круглую трубу с опорой и арматурой.

#### Этап 1: Подготовка и настройка профиля

```cpp
vCSDragManager* pDM = vCSDragManager::DM();
if (pDM && pDM->getSettingsTracing())
{
    if (pDM->getSettingsTracing()->IsRectProfile())
    {
        pDM->getSettingsTracing()->m_bRectProfile = false;
        LogMessage(L"Settings: switched to round profile");
    }
}
```

**Назначение**: Убеждается, что используется круглый профиль (не прямоугольный).

**Алгоритм:**
1. Получает глобальный экземпляр `vCSDragManager`
2. Проверяет настройки трассировки
3. Если профиль прямоугольный, переключает на круглый

---

#### Этап 2: Определение точек пути трубы

```cpp
AcGePoint3d ptStart(0.0, 0.0, 0.0);
AcGePoint3d ptMiddle(5000.0, 0.0, 0.0);
AcGePoint3d ptEnd(5000.0, 5000.0, 0.0);

AcGePoint3dArray path;
path.append(ptStart);
path.append(ptMiddle);
path.append(ptEnd);
```

**Назначение**: Создает три точки для определения пути трубы.

**Точки:**
- **Начальная точка**: `(0, 0, 0)` - начало координат
- **Средняя точка**: `(5000, 0, 0)` - 5 метров по оси X
- **Конечная точка**: `(5000, 5000, 0)` - 5 метров по оси Y (угол 90°)

**Результат**: Труба будет иметь Г-образную форму: горизонтальная часть 5м, затем вертикальный поворот на 90° на 5м.

---

#### Этап 3: Создание круглого профиля

```cpp
vCSProfilePtr pProfile = new vCSProfileCircle(true, 108.0, 100.0);
```

**Назначение**: Создает круглый профиль трубы.

**Параметры `vCSProfileCircle`:**
- `true` - булево значение (возможно, флаг активности)
- `108.0` - внешний диаметр трубы в миллиметрах
- `100.0` - условный проход (DN) в миллиметрах

**Спецификация трубы:**
- **Внешний диаметр**: 108 мм
- **Условный проход (DN)**: 100 мм
- **Тип**: Круглая (не прямоугольная)

---

#### Этап 4: Создание трубы

```cpp
AcDbObjectId idLCSN_Start = AcDbObjectId::kNull;
AcDbObjectId idLCSN_End = AcDbObjectId::kNull;

CreatePipeOnPointsWithProfiles cpop(path, pProfile, pProfile, pProfile);
Acad::ErrorStatus es = cpop.CalculateAndConnect(idLCSN_Start, idLCSN_End);
```

**Назначение**: Создает трубу по заданным точкам с указанным профилем.

**Важные замечания:**
- `idLCSN_Start` и `idLCSN_End` передаются **по значению** (не по ссылке)
- Эти параметры используются для **подключения к существующим узлам**, а не для возврата созданных
- Для создания новой трубы передается `AcDbObjectId::kNull`, что означает создание новых узлов

**Параметры конструктора `CreatePipeOnPointsWithProfiles`:**
- `path` - массив точек пути
- `pProfile` (начальный) - профиль для начала трубы
- `pProfile` (промежуточный) - профиль для промежуточных сегментов
- `pProfile` (конечный) - профиль для конца трубы

**В данном случае** все три профиля одинаковые (круглый, 108/100).

**Метод `CalculateAndConnect()`:**
- Выполняет расчет и создание трубы в базе данных
- Возвращает `Acad::ErrorStatus` - код ошибки/успеха

**Обработка ошибок:**
```cpp
if (es == Acad::eInvalidOffset || es != Acad::eOk)
{
    acutPrintf(L"\nERROR: Failed to create pipe (code: %d).", es);
    LogMessage(L"Error creating pipe, code: %d", es);
    return;
}
```

---

#### Этап 5: Получение ID оси трубы

**Проблема**: После создания трубы необходимо получить `AcDbObjectId` оси для дальнейших операций (добавление опор, арматуры).

**Реализовано три способа получения оси (fallback механизм):**

##### Способ 1: Через `getCalculatedAxis()` (основной)

```cpp
vCS_DM_Axis* pDM_Axis = cpop.getCalculatedAxis();
if (pDM_Axis)
{
    pipeAxisId = pDM_Axis->OID();
    LogMessage(L"Got axis ID from getCalculatedAxis: %ld", pipeAxisId.asOldId());
}
```

**Назначение**: Прямое получение оси из объекта `CreatePipeOnPointsWithProfiles`.

**Преимущества**: Самый надежный способ, если объект еще активен.

---

##### Способ 2: Через Drag Manager (альтернативный 1)

```cpp
if (pipeAxisId.isNull())
{
    vCSDragManager* pDM = vCSDragManager::DM();
    if (pDM)
    {
        AcDbObjectIdArray arrNewAxises;
        pDM->GetOIdNewAxises(arrNewAxises);
        if (arrNewAxises.length() > 0)
        {
            pipeAxisId = arrNewAxises[0];
            LogMessage(L"Got axis ID from Drag Manager GetOIdNewAxises: %ld", pipeAxisId.asOldId());
        }
    }
}
```

**Назначение**: Получает список всех новых осей, созданных в текущей сессии Drag Manager.

**Использует**: Метод `GetOIdNewAxises()` класса `vCSDragManager`, который возвращает массив ID всех осей, созданных после последнего `Clear()`.

**Применение**: Если Drag Manager активен и отслеживает созданные объекты.

---

##### Способ 3: Через поиск сегментов в базе данных (альтернативный 2)

```cpp
if (pipeAxisId.isNull())
{
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    AcDbBlockTable* pBlockTable = nullptr;
    if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) == Acad::eOk)
    {
        AcDbBlockTableRecord* pModelSpace = nullptr;
        if (pBlockTable->getAt(ACDB_MODEL_SPACE, pModelSpace, AcDb::kForRead) == Acad::eOk)
        {
            AcDbBlockTableRecordIterator* pIter = nullptr;
            if (pModelSpace->newIterator(pIter) == Acad::eOk)
            {
                AcDbObjectId lastSegAxisId = AcDbObjectId::kNull;
                for (pIter->start(); !pIter->done(); pIter->step())
                {
                    AcDbEntity* pEnt = nullptr;
                    if (pIter->getEntity(pEnt, AcDb::kForRead) == Acad::eOk)
                    {
                        vCSSegment2* pSeg = vCSSegment2::cast(pEnt);
                        if (pSeg)
                        {
                            AcDbObjectId segAxisId = pSeg->GetOIdAxis();
                            if (!segAxisId.isNull())
                            {
                                lastSegAxisId = segAxisId;
                            }
                        }
                        pEnt->close();
                    }
                }
                delete pIter;
                pipeAxisId = lastSegAxisId;
            }
            pModelSpace->close();
        }
        pBlockTable->close();
    }
}
```

**Назначение**: Находит ось через поиск сегментов в базе данных Model Space.

**Алгоритм:**
1. Получает рабочую базу данных AutoCAD
2. Открывает BlockTable и ModelSpace для чтения
3. Итерирует по всем сущностям в ModelSpace
4. Ищет объекты типа `vCSSegment2` (сегменты труб)
5. Для каждого найденного сегмента получает ID его оси через `GetOIdAxis()`
6. Сохраняет последний найденный ID оси (предполагается, что это наша только что созданная труба)

**Преимущества**: Работает даже если предыдущие способы не сработали.

**Недостатки**: Может найти не ту ось, если в базе данных много труб.

---

#### Этап 6: Подготовка Drag Manager для добавления элементов

```cpp
pDM->End();
pDM->Clear();

if (!pDM->setAcGsViewForViewPort(true))
{
    acutPrintf(L"\nWARNING: Failed to set AcGsView");
    LogMessage(L"Warning: failed to set AcGsView");
}

pDM->Start(pipeAxisId);
vCS_DM_Axis* pAxis = pDM->GetAxis(pipeAxisId);
```

**Назначение**: Инициализирует Drag Manager для работы с созданной осью.

**Операции:**
- `End()` - завершает текущую сессию Drag Manager (если активна)
- `Clear()` - очищает внутренние структуры
- `setAcGsViewForViewPort(true)` - устанавливает графический вид для визуализации операций
- `Start(pipeAxisId)` - начинает работу с указанной осью
- `GetAxis(pipeAxisId)` - получает указатель на `vCS_DM_Axis` для дальнейших операций

**Проверка успешности:**
```cpp
if (!pAxis)
{
    acutPrintf(L"\nERROR: Failed to get axis for adding elements.");
    pDM->End();
    return;
}

if (pAxis->GetSegCount() == 0)
{
    acutPrintf(L"\nERROR: No segments on pipe.");
    pDM->End();
    return;
}
```

---

#### Этап 7: Получение сегмента

```cpp
vCS_DM_Seg* pSeg = pAxis->GetSeg(0);
```

**Назначение**: Получает первый сегмент оси для добавления элементов.

**Примечание**: В данном примере используется первый сегмент (индекс 0). Для более сложных труб с несколькими сегментами может потребоваться итерация.

**Вычисление параметров сегмента:**
```cpp
AcGePoint3d segStart = pSeg->GetStartPoint();
AcGePoint3d segEnd = pSeg->GetEndPoint();
double segLength = segStart.distanceTo(segEnd);
```

**Методы `vCS_DM_Seg`:**
- `GetStartPoint()` - возвращает начальную точку сегмента
- `GetEndPoint()` - возвращает конечную точку сегмента

**Расчет длины**: Используется метод `distanceTo()` класса `AcGePoint3d`.

---

#### Этап 8: Добавление опоры

```cpp
double supportOffset = segLength * 0.3;  // 30% от длины сегмента

vCS_DM_Support* pSupport = pSeg->AddSupport(supportOffset, st_Support);
if (pSupport)
{
    pSupport->SetDMAxis(pAxis);
    pSupport->SetSeg(pSeg);
    
    AcGeVector3d segDir = (segEnd - segStart).normal();
    AcGePoint3d supportPoint = segStart + segDir * supportOffset;
    pSupport->SetBasePoint(supportPoint);
    
    LogMessage(L"Support created, ID: %ld", pSupport->OID().asOldId());
    acutPrintf(L"\nOK: Support added at distance %.2f mm", supportOffset);
}
```

**Назначение**: Добавляет опору на трубу на расстоянии 30% от начала сегмента.

**Параметры `AddSupport()`:**
- `supportOffset` - расстояние от начала сегмента до опоры (в единицах чертежа, обычно мм)
- `st_Support` - тип опоры (enum `eSupportType`, значение `st_Support`)

**Настройка опоры:**
- `SetDMAxis(pAxis)` - устанавливает связь с осью
- `SetSeg(pSeg)` - устанавливает связь с сегментом
- `SetBasePoint(supportPoint)` - устанавливает базовую точку опоры

**Расчет позиции:**
1. Вычисляется направление сегмента: `segDir = (segEnd - segStart).normal()`
2. Вычисляется точка опоры: `supportPoint = segStart + segDir * supportOffset`

**Типы опор (`eSupportType`):**
- `st_Support` - стандартная опора (используется в коде)
- Другие типы могут быть определены в SDK

---

#### Этап 9: Добавление арматуры (Inline)

```cpp
double inlineOffset = segLength * 0.7;  // 70% от длины сегмента

vCS_DM_InLine* pInline = pSeg->AddInLine(inlineOffset, static_cast<unsigned int>(vCSInLine::til_inline));
if (pInline)
{
    pInline->SetDMAxis(pAxis);
    pInline->SetSeg(pSeg);
    
    AcGeVector3d segDir = (segEnd - segStart).normal();
    AcGePoint3d inlinePoint = segStart + segDir * inlineOffset;
    pInline->SetBasePoint(inlinePoint);
    
    LogMessage(L"Inline created, ID: %ld", pInline->OID().asOldId());
    acutPrintf(L"\nOK: Inline added at distance %.2f mm", inlineOffset);
}
```

**Назначение**: Добавляет арматуру (inline элемент) на расстоянии 70% от начала сегмента.

**Параметры `AddInLine()`:**
- `inlineOffset` - расстояние от начала сегмента до арматуры
- `static_cast<unsigned int>(vCSInLine::til_inline)` - тип inline элемента

**Типы арматуры (`vCSInLine::ETypeInLine`):**
- `til_inline` - стандартная арматура (используется в коде)
- `til_weld` - сварной шов
- `til_reducer` - переходник (редуктор)
- Другие типы могут быть определены в SDK

**Примечание**: Используется `static_cast<unsigned int>()`, так как метод `AddInLine()` принимает `unsigned int`, а не enum напрямую.

**Настройка арматуры**: Аналогично опоре - устанавливаются связи с осью и сегментом, вычисляется базовая точка.

---

#### Этап 10: Пересчет модели и обновление базы данных

```cpp
pDM->ArrPtrEditableAxis_Add(pAxis);
pDM->SetDragType(vCS::eReCalculate);
Acad::ErrorStatus calcStatus = pDM->ReCalculateModelMain();
if (calcStatus == Acad::eOk)
{
    pDM->End();
    pDM->CheckForErase();
    pDM->UpdateDBEnt();
    acutPrintf(L"\nOK: Model recalculated and updated");
    LogMessage(L"Model successfully recalculated and saved");
}
else
{
    acutPrintf(L"\nWARNING: Model recalculation error (code: %d)", calcStatus);
    LogMessage(L"Model recalculation error, code: %d", calcStatus);
    pDM->End();
}

pDM->Clear();
```

**Назначение**: Применяет все изменения к модели и сохраняет в базе данных.

**Последовательность операций:**

1. **`ArrPtrEditableAxis_Add(pAxis)`** - добавляет ось в список редактируемых осей для пересчета

2. **`SetDragType(vCS::eReCalculate)`** - устанавливает тип операции Drag Manager как "пересчет"
   - `vCS::eReCalculate` - пересчет модели с учетом всех изменений

3. **`ReCalculateModelMain()`** - выполняет основной пересчет модели
   - Пересчитывает геометрию трубы с учетом добавленных опор и арматуры
   - Обновляет все связи между элементами
   - Возвращает `Acad::ErrorStatus` - код успеха/ошибки

4. **`CheckForErase()`** - проверяет, нужно ли удалить какие-либо объекты (например, при изменении геометрии)

5. **`UpdateDBEnt()`** - обновляет все измененные объекты в базе данных AutoCAD
   - Сохраняет опоры, арматуру и изменения геометрии трубы

6. **`End()`** - завершает сессию Drag Manager

7. **`Clear()`** - очищает внутренние структуры для следующей операции

**Обработка ошибок**: Если пересчет не удался, выводится предупреждение, но сессия Drag Manager все равно завершается.

---

### Точка входа: `ncrxEntryPoint()`

```cpp
extern "C" __declspec(dllexport) AcRx::AppRetCode ncrxEntryPoint(AcRx::AppMsgCode msg, void* appId)
```

**Назначение**: Стандартная точка входа для nanoCAD/ObjectARX плагинов.

**Параметры:**
- `msg` - код сообщения (инициализация, выгрузка, и т.д.)
- `appId` - идентификатор приложения

**Обрабатываемые сообщения:**

##### `AcRx::kInitAppMsg` (инициализация плагина)

```cpp
case AcRx::kInitAppMsg:
    acrxDynamicLinker->unlockApplication(appId);
    acrxDynamicLinker->registerAppMDIAware(appId);
    
    acedRegCmds->addCommand(L"PIPE_TEST_GROUP",
        L"_CREATETESTPIPE", L"CREATETESTPIPE",
        ACRX_CMD_MODAL, createTestPipe);
    break;
```

**Операции:**
1. **`unlockApplication(appId)`** - разблокирует приложение для выгрузки
2. **`registerAppMDIAware(appId)`** - регистрирует плагин как MDI-aware (поддержка многодокументного интерфейса)
3. **`addCommand()`** - регистрирует команду AutoCAD

**Параметры `addCommand()`:**
- `L"PIPE_TEST_GROUP"` - группа команд (для группового управления)
- `L"_CREATETESTPIPE"` - имя команды для вызова из командной строки (с подчеркиванием для поддержки всех языков)
- `L"CREATETESTPIPE"` - имя команды без подчеркивания
- `ACRX_CMD_MODAL` - тип команды (модальная - блокирует ввод до завершения)
- `createTestPipe` - указатель на функцию-обработчик

**Использование в AutoCAD/nanoCAD:**
```
Command: CREATETESTPIPE
или
Command: _CREATETESTPIPE
```

##### `AcRx::kUnloadAppMsg` (выгрузка плагина)

```cpp
case AcRx::kUnloadAppMsg:
    acedRegCmds->removeGroup(L"PIPE_TEST_GROUP");
    break;
```

**Операции:**
- **`removeGroup()`** - удаляет зарегистрированную группу команд при выгрузке плагина

**Возвращаемое значение:**
- `AcRx::kRetOK` - успешная обработка

---

## Обработка ошибок

### Try-Catch блоки

```cpp
try
{
    // Основной код
}
catch (const std::exception& ex)
{
    LogMessage(L"std::exception in createTestPipe: %hs", ex.what());
    acutPrintf(L"\nERROR: %hs", ex.what());
}
catch (...)
{
    LogMessage(L"Unknown error in createTestPipe");
    acutPrintf(L"\nERROR: Unknown error occurred.");
}
```

**Назначение**: Перехватывает все исключения C++ для предотвращения краша приложения.

**Уровни обработки:**
1. **Стандартные исключения C++** (`std::exception`) - логируются с сообщением
2. **Любые другие исключения** (`...`) - логируются как "неизвестная ошибка"

**Важно**: Все исключения логируются в файл `%TEMP%\HelloNRX.log` и выводятся пользователю через `acutPrintf()`.

---

## Логирование

### Формат логов

```
[YYYY-MM-DD HH:MM:SS.mmm] Сообщение
```

**Пример:**
```
[2026-01-10 14:23:45.123] BEGIN createTestPipe
[2026-01-10 14:23:45.125] Points: Start=(0,0,0), Middle=(5000,0,0), End=(5000,5000,0)
[2026-01-10 14:23:45.200] Created round profile: diameter=108.0, DN=100.0
[2026-01-10 14:23:45.350] Pipe created, axis ID: 123456
[2026-01-10 14:23:45.400] END createTestPipe - success
```

### Расположение лог-файла

- **Путь**: `%TEMP%\HelloNRX.log`
- **Пример полного пути**: `C:\Users\<Username>\AppData\Local\Temp\HelloNRX.log`
- **Режим записи**: Добавление (append), не перезапись
- **Кодировка**: UTF-16 (wide string)

### Просмотр логов

1. **В Visual Studio**: Откройте окно "Output" (Debug Output) во время отладки
2. **В файле**: Откройте `%TEMP%\HelloNRX.log` в текстовом редакторе
3. **Быстрый доступ**: Нажмите Win+R, введите `%TEMP%`, найдите `HelloNRX.log`

---

## Используемые типы данных и классы

### AutoCAD/ObjectARX типы

#### `AcGePoint3d`
- **Описание**: 3D точка (x, y, z) с плавающей точкой
- **Поля**: `x`, `y`, `z` (тип `double`)
- **Методы**:
  - `distanceTo(const AcGePoint3d&)` - расстояние до другой точки
  - `distanceTo(const AcGePoint3d&)` - нормализация вектора

#### `AcGePoint3dArray`
- **Описание**: Динамический массив 3D точек
- **Методы**:
  - `append(const AcGePoint3d&)` - добавление точки
  - `length()` - количество точек

#### `AcGeVector3d`
- **Описание**: 3D вектор (для направлений)
- **Методы**:
  - `normal()` - нормализация вектора (возвращает единичный вектор)

#### `AcDbObjectId`
- **Описание**: Идентификатор объекта в базе данных AutoCAD
- **Специальные значения**:
  - `AcDbObjectId::kNull` - пустой/null ID
- **Методы**:
  - `isNull()` - проверка на null
  - `asOldId()` - получение числового представления (для логирования)

#### `Acad::ErrorStatus`
- **Описание**: Код ошибки/успеха операции AutoCAD
- **Значения**:
  - `Acad::eOk` - успех
  - `Acad::eInvalidOffset` - неверное смещение
  - Другие коды ошибок (см. документацию ObjectARX)

### ViperCS/Model Studio классы

#### `vCSDragManager`
- **Описание**: Менеджер для управления операциями редактирования трубопроводной системы
- **Методы (используемые в коде)**:
  - `DM()` - получение глобального экземпляра (singleton)
  - `getSettingsTracing()` - получение настроек трассировки
  - `End()` - завершение сессии
  - `Clear()` - очистка внутренних структур
  - `Start(AcDbObjectId)` - начало работы с осью
  - `GetAxis(AcDbObjectId)` - получение оси по ID
  - `GetOIdNewAxises(AcDbObjectIdArray&)` - получение списка созданных осей
  - `setAcGsViewForViewPort(bool)` - установка графического вида
  - `ArrPtrEditableAxis_Add(vCS_DM_Axis*)` - добавление оси для редактирования
  - `SetDragType(vCS::eDragType)` - установка типа операции
  - `ReCalculateModelMain()` - пересчет модели
  - `CheckForErase()` - проверка на удаление объектов
  - `UpdateDBEnt()` - обновление объектов в базе данных

#### `vCSProfileCircle`
- **Описание**: Круглый профиль трубы
- **Конструктор**: `vCSProfileCircle(bool, double diameter, double DN)`
- **Параметры**:
  - Диаметр внешний (мм)
  - Условный проход DN (мм)

#### `vCSProfilePtr`
- **Описание**: Умный указатель (smart pointer) на профиль
- **Тип**: `typedef` для автоматического управления памятью

#### `CreatePipeOnPointsWithProfiles`
- **Описание**: Класс для создания трубы по точкам с заданными профилями
- **Конструктор**: `CreatePipeOnPointsWithProfiles(AcGePoint3dArray&, vCSProfileBase*, vCSProfileBase*, vCSProfileBase*)`
- **Методы**:
  - `CalculateAndConnect(AcDbObjectId, AcDbObjectId)` - расчет и создание трубы
  - `getCalculatedAxis()` - получение созданной оси

#### `vCS_DM_Axis`
- **Описание**: Представление оси в Drag Manager
- **Методы (используемые в коде)**:
  - `OID()` - получение ID оси
  - `GetSegCount()` - количество сегментов
  - `GetSeg(int index)` - получение сегмента по индексу

#### `vCS_DM_Seg`
- **Описание**: Представление сегмента в Drag Manager
- **Методы (используемые в коде)**:
  - `GetStartPoint()` - начальная точка сегмента
  - `GetEndPoint()` - конечная точка сегмента
  - `AddSupport(double offset, eSupportType)` - добавление опоры
  - `AddInLine(double offset, unsigned int type)` - добавление арматуры

#### `vCS_DM_Support`
- **Описание**: Представление опоры в Drag Manager
- **Методы (используемые в коде)**:
  - `SetDMAxis(vCS_DM_Axis*)` - установка связи с осью
  - `SetSeg(vCS_DM_Seg*)` - установка связи с сегментом
  - `SetBasePoint(AcGePoint3d)` - установка базовой точки
  - `OID()` - получение ID опоры

#### `vCS_DM_InLine`
- **Описание**: Представление арматуры в Drag Manager
- **Методы**: Аналогично `vCS_DM_Support`

#### `vCSSegment2`
- **Описание**: Сегмент трубы в базе данных
- **Методы (используемые в коде)**:
  - `GetOIdAxis()` - получение ID оси сегмента
  - `cast(AcDbEntity*)` - приведение типа (static cast)

---

## Алгоритм работы (пошагово)

1. **Инициализация плагина** (`ncrxEntryPoint` с `kInitAppMsg`)
   - Регистрация команды `CREATETESTPIPE`

2. **Пользователь вводит команду** `CREATETESTPIPE`

3. **Вызов функции `createTestPipe()`**

4. **Настройка профиля** (круглый, не прямоугольный)

5. **Определение точек пути** (3 точки: начало, середина, конец)

6. **Создание круглого профиля** (диаметр 108 мм, DN 100)

7. **Создание трубы** через `CreatePipeOnPointsWithProfiles::CalculateAndConnect()`

8. **Получение ID оси** (три способа с fallback)

9. **Инициализация Drag Manager** для работы с осью

10. **Получение первого сегмента** оси

11. **Расчет позиций** опоры (30%) и арматуры (70%)

12. **Добавление опоры** через `pSeg->AddSupport()`

13. **Добавление арматуры** через `pSeg->AddInLine()`

14. **Пересчет модели** через `ReCalculateModelMain()`

15. **Обновление базы данных** через `UpdateDBEnt()`

16. **Завершение операции** и очистка Drag Manager

---

## Особенности реализации

### Проблема получения оси

После создания трубы через `CreatePipeOnPointsWithProfiles::CalculateAndConnect()`, объект `CreatePipeOnPointsWithProfiles` может не сохранять ссылку на созданную ось в некоторых случаях. Поэтому реализовано три способа получения оси с fallback механизмом.

### Использование Drag Manager

Все операции добавления элементов (опор, арматуры) выполняются через Drag Manager, а не напрямую в базе данных. Это обеспечивает:
- Корректную обработку геометрии
- Автоматический пересчет связей
- Правильное обновление визуализации

### Позиционирование элементов

Опора и арматура позиционируются относительно начала сегмента, а не в абсолютных координатах. Это обеспечивает правильное размещение даже при изменении геометрии трубы.

### Обработка ошибок

Все критичные операции проверяются на успешность, и при ошибке выполнение прекращается с логированием. Это предотвращает краши и упрощает отладку.

---

## Требования

### Системные требования
- **ОС**: Windows 10/11 (64-bit)
- **CAD**: nanoCAD или Model Studio CS с поддержкой ViperCS SDK
- **Visual Studio**: 2019 или выше (MSVC 1920+)
- **Стандарт C++**: C++11 или выше

### SDK требования
- **ObjectARX SDK**: Версия 202X или совместимая
- **ViperCS SDK**: Версия, соответствующая версии Model Studio
- **Windows SDK**: Версия 10.0 или выше

### Зависимости
- **MFC**: Для строковых операций (`CString`, если используется)
- **ATL**: Для COM поддержки (если требуется)

---

## Компиляция

### Настройки проекта

1. **Платформа**: x64
2. **Конфигурация**: Release/Debug
3. **Тип проекта**: Dynamic Library (DLL)
4. **Runtime Library**: Multi-threaded DLL (`/MD` для Release, `/MDd` для Debug)

### Пути включения (Include Paths)

```
$(ARX_ROOT)\inc
$(VIPERCS_ROOT)\include
$(VIPERCS_ROOT)\ViperCSObj
```

### Библиотеки (Libraries)

```
acad.lib
acge.lib
rxapi.lib
vCSCreate.lib
vCSDragManager.lib
vCSObj.lib
```

### Флаги компилятора

- `/D "_ARXVER_202X"` - версия ObjectARX
- `/D "NCAD"` - поддержка nanoCAD
- `/D "_MSC_VER=1920"` - версия MSVC
- `/EHsc` - обработка исключений C++
- `/W3` - уровень предупреждений (можно повысить до `/W4`)

---

## Использование

### Установка

1. Скомпилируйте проект в конфигурации Release
2. Скопируйте полученный `.arx` файл в папку плагинов nanoCAD/Model Studio
3. Загрузите плагин через команду `NETLOAD` или автоматически при старте

### Запуск команды

В командной строке AutoCAD/nanoCAD:
```
Command: CREATETESTPIPE
```

или

```
Command: _CREATETESTPIPE
```

### Результат

- Создается круглая труба по 3 точкам (Г-образная форма)
- Добавляется опора на расстоянии 30% от начала первого сегмента
- Добавляется арматура на расстоянии 70% от начала первого сегмента
- Модель пересчитывается и сохраняется в базу данных

### Просмотр результата

- Труба, опора и арматура должны быть видны в графическом окне
- Все объекты доступны для дальнейшего редактирования
- Логи записываются в `%TEMP%\HelloNRX.log`

---

## Отладка

### Использование Visual Studio Debugger

1. Установите точку останова в функции `createTestPipe()`
2. Настройте проект на запуск nanoCAD/Model Studio как внешней программы
3. Запустите отладку (F5)
4. В nanoCAD выполните команду `CREATETESTPIPE`
5. Отладчик остановится на точке останова

### Логирование

Все операции логируются в файл `%TEMP%\HelloNRX.log`. Формат:
```
[YYYY-MM-DD HH:MM:SS.mmm] Сообщение
```

### Типичные проблемы

#### Проблема: Ось не найдена

**Симптомы**: Ошибка "Failed to get pipe axis"

**Решение**: Проверьте логи - какой из трех способов получения оси использовался. Если все три способа не сработали, возможно:
- Труба не была создана (проверьте код ошибки `CalculateAndConnect`)
- База данных заблокирована (проверьте транзакции)

#### Проблема: Опора/арматура не отображается

**Симптомы**: Труба создана, но опора и арматура не видны

**Решение**:
- Проверьте, что `ReCalculateModelMain()` вернул `Acad::eOk`
- Проверьте, что `UpdateDBEnt()` был вызван
- Проверьте, что Drag Manager был правильно завершен (`End()`)

#### Проблема: Труба создается прямоугольной вместо круглой

**Симптомы**: Труба имеет прямоугольное сечение

**Решение**:
- Проверьте, что `vCSProfileCircle` создан правильно
- Проверьте, что `m_bRectProfile` установлен в `false`
- Убедитесь, что профиль передается во все три параметра конструктора `CreatePipeOnPointsWithProfiles`

---

## Расширение функциональности

### Возможные улучшения

1. **Интерактивный выбор точек**: Вместо фиксированных точек, позволить пользователю выбирать точки в графическом окне
2. **Выбор типа профиля**: Добавить диалог для выбора диаметра и DN
3. **Выбор типа опоры/арматуры**: Добавить параметры для выбора различных типов опор и арматуры
4. **Обработка нескольких сегментов**: Добавление опор/арматуры на все сегменты, а не только первый
5. **Экспорт/импорт параметров**: Сохранение и загрузка конфигураций трубы
6. **Валидация геометрии**: Проверка корректности созданной геометрии перед сохранением

### Пример: Интерактивный выбор точек

```cpp
void createTestPipeInteractive()
{
    AcGePoint3d ptStart, ptMiddle, ptEnd;
    
    // Запрос точек у пользователя
    if (acedGetPoint(NULL, L"\nSpecify start point: ", asDblArray(ptStart)) != RTNORM)
        return;
    
    if (acedGetPoint(asDblArray(ptStart), L"\nSpecify middle point: ", asDblArray(ptMiddle)) != RTNORM)
        return;
    
    if (acedGetPoint(asDblArray(ptMiddle), L"\nSpecify end point: ", asDblArray(ptEnd)) != RTNORM)
        return;
    
    // Далее аналогично createTestPipe()
    // ...
}
```

---

## Заключение

Код `HelloNRX.cpp` представляет собой полнофункциональный плагин для автоматического создания тестовой трубы с опорой и арматурой. Реализована надежная обработка ошибок, подробное логирование и несколько способов получения необходимых объектов для обеспечения стабильной работы.

Проект готов к использованию и может служить основой для более сложных плагинов по работе с трубопроводными системами в nanoCAD/Model Studio.

---

## Контакты и поддержка

- **Проект**: HelloNRX
- **Версия**: 1.0
- **Дата**: 2026-01-10
- **Платформа**: nanoCAD / Model Studio CS

---

**Примечание**: Данная документация описывает текущую версию кода. При изменении кода рекомендуется обновлять документацию соответствующим образом.
