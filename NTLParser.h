#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <atlstr.h>
#include "acdb.h"
#include "gepnt3d.h"
#include "geassign.h"

// Структура для данных сегмента трубы из NTL
struct NTLSegment
{
    CString name;              // Имя сегмента (например, "A00")
    CString segmentId;         // Идентификатор ветки (вторая колонка SEG, например "A")
    AcGePoint3d startPoint;    // Начальная точка
    AcGePoint3d endPoint;      // Конечная точка
    double diameter;           // Диаметр трубы
    double wallThickness;      // Толщина стенки
    double length;             // Длина трубы
    CString pipeName;          // Имя трубы
};

// Структура для инлайновых элементов (арматура/переходы/тройники)
struct NTLInline
{
    enum class Type
    {
        Inline,     // VALV / FLA и прочее
        Reducer,    // RED
        Tee         // TEE
    };
    Type type;
    CString name;          // Имя точки/элемента
    CString segmentId;     // Ветка (из SEG)
    AcGePoint3d position;  // Абсолютная позиция (рассчитанная, опционально)
    double distance = 0.0; // Смещение вдоль ветки (от начала сегмента/цепи)
};

// Структура для данных опоры из NTL
struct NTLSupport
{
    CString name;              // Имя опоры (например, "A01")
    AcGePoint3d position;      // Позиция опоры
    CString supportType;       // Тип опоры (например, "Y1")
    double distance;           // Расстояние от начала
};

// Структура для данных операции из NTL
struct NTLOperation
{
    CString name;              // Имя операции (например, "A00")
    double temperature;        // Температура
    double pressure;           // Давление
};

// Класс для парсинга NTL файлов
class CNTLParser
{
public:
    CNTLParser();
    virtual ~CNTLParser();

    // Открыть и прочитать NTL файл
    bool ReadFile(const CString& filePath);
    
    // Получить все сегменты
    const std::vector<NTLSegment>& GetSegments() const { return m_segments; }
    
    // Получить все опоры
    const std::vector<NTLSupport>& GetSupports() const { return m_supports; }
    
    // Получить все инлайны (арматура/переходы/тройники)
    const std::vector<NTLInline>& GetInlines() const { return m_inlines; }
    
    // Получить все операции
    const std::vector<NTLOperation>& GetOperations() const { return m_operations; }
    
    // Очистить данные
    void Clear();

protected:
    // Парсинг строки SEG (сегмент)
    bool ParseSegment(const CString& line);
    // Парсинг строки PIPE (продолжение SEG на новой строке)
    bool ParsePipe(const CString& line);
    
    // Парсинг строки SPRG (опора)
    bool ParseSupport(const CString& line);
    
    // Парсинг строки OPER (операция)
    bool ParseOperation(const CString& line);
    
    // Парсинг строки RUN (участок)
    bool ParseRun(const CString& line);
    // Парсинг строки BEND (как RUN, но сохраняем сегмент)
    bool ParseBend(const CString& line);
    // Парсинг VALV/FLA/RED/TEE
    bool ParseInlineValv(const CString& line);
    bool ParseInlineFla(const CString& line);
    bool ParseInlineRed(const CString& line);
    bool ParseInlineTee(const CString& line);
    
    // Разбор строки на токены (разделитель - пробел)
    std::vector<CString> Tokenize(const CString& line) const;
    
    // Преобразование строки в число
    double StringToDouble(const CString& str) const;
    
    // Преобразование строки в int
    int StringToInt(const CString& str) const;

private:
    std::vector<NTLSegment> m_segments;
    std::vector<NTLInline> m_inlines;
    std::vector<NTLSupport> m_supports;
    std::vector<NTLOperation> m_operations;
    
    // Текущая позиция для расчета опор (аккумулируемая длина)
    double m_currentDistance;
    AcGePoint3d m_lastPoint;

    // Текущие параметры трубы (последняя строка PIPE)
    double m_currentDiameter;
    double m_currentWallThickness;
    double m_currentOD;
    CString m_currentPipeName;
    CString m_lastSegName;
    CString m_currentSegmentId;

    // Создать сегмент от m_lastPoint до newPoint с текущими параметрами трубы
    bool CreateSegmentTo(const AcGePoint3d& newPoint);
};
