# Экспорт арматуры с координатами (команда `EXPORTARMATURE`)

В проект добавлен метод `exportArmatureTable()` и команда `EXPORTARMATURE`, которая собирает все арматурные объекты в модели и выгружает их в CSV. Работает как для ссылочных объектов (dummy), так и для обычных. Если в модели есть dummy‑экземпляры, в отчёт попадут только они; иначе — все арматурные сущности.

## Что собирает
- Класс сущности: по имени класса ищутся inline/valve/armatur (`IsArmatureClass`).
- Признак ссылочного объекта: имя класса содержит `dummy`.
- Параметры объекта: через `ursGetObjectParameters`.
- Параметр `KKS_PART`: объект включается в отчёт, только если параметр существует (пустое значение допускается).
- Координаты: центр геометрических экстентов сущности в мировой системе координат (WCS).

## Формат CSV
Файл создаётся в `%TEMP%\ArmatureTable.csv` в кодировке UTF‑8 с BOM. Колонки:

```
KKS_PART;X;Y;Z;ClassName;Dummy
```

`Dummy` — `1` для ссылочных объектов, `0` иначе.

## Команда
- Зарегистрирована в группе `PIPE_TEST_GROUP`.
- Имя команды: `EXPORTARMATURE` (и `_EXPORTARMATURE`).

## Зависимости/инклюды
Для переноса кода в другой проект понадобятся:
- ObjectARX/nanoCAD SDK: `acdb.h`, `dbmain.h`, `dbapserv.h`, `dbtable.h`, `dbents.h`, `geassign.h`, `rxregsvc.h`, `acgi.h`, `aced.h`, `dbxutil.h`.
- Model Studio/ViperCS SDK: `ursUtils.h` (для `ursGetObjectParameters`), `ParamsObject.h` (класс `CElement`), `ParamDefs.h` (определения параметров), плюс стандартные заголовки проекта (`stdafx.h`/PCH).
- STL: `<vector>`, `<algorithm>`, `<string>`, `<sstream>`, `<fstream>`, `<cwctype>`, `<cwchar>`.

## Кратко о реализации
- Фильтр арматуры: `IsArmatureClass` проверяет имя класса на подстроки `inline/valve/armatur`.
- Проверка ссылочного объекта: `IsDummyClass` ищет `dummy` в имени класса.
- Параметры: `ursGetObjectParameters(objectId, CElement)`; затем `GetValue("KKS_PART", CString&)`.
- Геометрия: `getGeomExtents` и центр бокса как XYZ.
- Выбор подмножества: если найдены dummy, берём только их, иначе — всё найденное.

## Использование
1. Загрузите модель (при необходимости — ссылочными объектами).
2. Запустите команду `EXPORTARMATURE`.
3. Откройте `%TEMP%\ArmatureTable.csv`.

