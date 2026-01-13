#include "stdafx.h"
#include "NTLParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include "geassign.h"
#include <atlstr.h>
#include <cctype>
#include "acdb.h"
#include "gepnt3d.h"
#include <afx.h>
#include <afxwin.h>

CNTLParser::CNTLParser()
    : m_currentDistance(0.0)
    , m_lastPoint(0.0, 0.0, 0.0)
    , m_currentDiameter(0.0)
    , m_currentWallThickness(0.0)
    , m_currentOD(0.0)
    , m_currentPipeName()
    , m_lastSegName()
    , m_currentSegmentId()
{
}

CNTLParser::~CNTLParser()
{
    Clear();
}

void CNTLParser::Clear()
{
    m_segments.clear();
    m_inlines.clear();
    m_supports.clear();
    m_operations.clear();
    m_currentDistance = 0.0;
    m_lastPoint = AcGePoint3d(0.0, 0.0, 0.0);
    m_currentDiameter = 0.0;
    m_currentWallThickness = 0.0;
    m_currentOD = 0.0;
    m_currentPipeName.Empty();
    m_lastSegName.Empty();
    m_currentSegmentId.Empty();
}

bool CNTLParser::ReadFile(const CString& filePath)
{
    Clear();

    try
    {
        // Используем CStdioFile для работы с Unicode путями
        CStdioFile file;
        if (!file.Open(filePath, CFile::modeRead | CFile::typeText))
        {
            return false;
        }

        CString line;
        while (file.ReadString(line))
        {
            line.TrimLeft();
            line.TrimRight();
            
            // Пропускаем пустые строки и комментарии (строки, начинающиеся с *)
            if (line.IsEmpty() || line.GetAt(0) == _T('*'))
            {
                continue;
            }

            // Определяем тип строки по первому слову
            CString firstWord = line.SpanExcluding(_T(" \t"));
            firstWord.MakeUpper();

            if (firstWord == _T("SEG"))
            {
                ParseSegment(line);
            }
        else if (firstWord == _T("PIPE"))
        {
            // Строка PIPE может идти отдельной строкой после SEG
            ParsePipe(line);
        }
            else if (firstWord == _T("SPRG"))
            {
                ParseSupport(line);
            }
            else if (firstWord == _T("OPER"))
            {
                ParseOperation(line);
            }
            else if (firstWord == _T("RUN"))
            {
        ParseRun(line);
            }
            else if (firstWord == _T("BEND"))
            {
                ParseBend(line);
            }
            else if (firstWord == _T("VALV"))
            {
                ParseInlineValv(line);
            }
            else if (firstWord == _T("FLA") || firstWord == _T("FLAA"))
            {
                ParseInlineFla(line);
            }
            else if (firstWord == _T("RED"))
            {
                ParseInlineRed(line);
            }
            else if (firstWord == _T("TEE"))
            {
                ParseInlineTee(line);
            }
        else if (firstWord == _T("***"))
        {
            // Линии типа "***  Pipe OD 8.625" или "***  Wall Thickness 0.322"
            CString rest = line.Mid(3);
            rest.TrimLeft();
            rest.MakeUpper();
            if (rest.Find(_T("PIPE OD")) >= 0)
            {
                // Читаем последнее число в строке
                std::vector<CString> t = Tokenize(line);
                double lastNum = 0.0;
                for (const auto& s : t)
                {
                    double v = StringToDouble(s);
                    if (v > 0.0)
                        lastNum = v;
                }
                if (lastNum > 0.0)
                    {
                        m_currentOD = lastNum;
                        // OD переопределяет текущий диаметр
                        m_currentDiameter = m_currentOD;
                    }
            }
            else if (rest.Find(_T("WALL THICKNESS")) >= 0)
            {
                std::vector<CString> t = Tokenize(line);
                double lastNum = 0.0;
                for (const auto& s : t)
                {
                    double v = StringToDouble(s);
                    if (v > 0.0)
                        lastNum = v;
                }
                if (lastNum > 0.0)
                    m_currentWallThickness = lastNum;
            }
        }
        }

        file.Close();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool CNTLParser::ParseSegment(const CString& line)
{
    // Формат может быть в две строки:
    //  SEG A00 A 0.000 0.000 0.000
    //  PIPE 123 N -123.000 12.000 0.000 1.5000 N
    // Поэтому здесь читаем только имя и стартовые координаты (если есть).
    // Диаметр и конец сегмента будут считаны в ParsePipe.
    
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)  // Нужны хотя бы "SEG" и имя
    {
        return false;
    }

    NTLSegment seg;
    
    // Имя сегмента (токен 1)
    seg.name = tokens[1];
    m_lastSegName = seg.name;
    // Идентификатор ветки (токен 2, если есть)
    if (tokens.size() > 2)
    {
        CString newSegId = tokens[2];
        bool isNewBranch = newSegId.CompareNoCase(m_currentSegmentId) != 0;
        seg.segmentId = newSegId;
        m_currentSegmentId = newSegId;
        // При переходе на новую ветку сбрасываем накопленную длину
        if (isNewBranch)
            m_currentDistance = 0.0;
    }
    else
    {
        seg.segmentId.Empty();
        m_currentSegmentId.Empty();
    }
    
    // Начальная точка (формат: SEG <name> <type> <x> <y> <z>)
    // Индексы координат: 3,4,5
    int coordIdx = 3;
    if ((int)tokens.size() > coordIdx)
        seg.startPoint.x = StringToDouble(tokens[coordIdx]);
    if ((int)tokens.size() > coordIdx + 1)
        seg.startPoint.y = StringToDouble(tokens[coordIdx + 1]);
    if ((int)tokens.size() > coordIdx + 2)
        seg.startPoint.z = StringToDouble(tokens[coordIdx + 2]);
    // Если координаты не указаны, используем последнюю известную точку
    if ((int)tokens.size() <= coordIdx)
        seg.startPoint = m_lastPoint;

    // По умолчанию конец совпадает со стартом, пока не придет PIPE
    seg.endPoint = seg.startPoint;

    // Имя трубы
    seg.pipeName = seg.name + _T("_PIPE");
    m_currentPipeName = seg.pipeName;

    // Сохраняем последнюю точку для следующих операций
    m_lastPoint = seg.startPoint;
    
    // Пока не добавляем сегмент, сегменты создаем по RUN/BEND
    return true;
}

bool CNTLParser::ParsePipe(const CString& line)
{
    // Обрабатываем строку PIPE, обновляем текущие параметры трубы (без создания сегмента).
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)
        return false;

    // Имя трубы (может быть текстовым идентификатором) — если не число
    CString token1 = tokens[1];
    token1.Trim();
    if (!token1.IsEmpty() && !isdigit(static_cast<unsigned char>(token1[0])))
        m_currentPipeName = token1;

    // Сбрасываем текущий OD, если пришла новая труба, будем переопределять
    m_currentOD = 0.0;

    // Диаметр/толщина: первое число после PIPE — OD/номинал, второе число — толщина (если есть)
    double diameter = 0.0;
    double thickness = 0.0;
    for (size_t i = 1; i < tokens.size(); ++i)
    {
        double val = StringToDouble(tokens[i]);
        if (val > 0.0 && diameter <= 0.0)
        {
            diameter = val;
            continue;
        }
        if (val > 0.0 && diameter > 0.0 && thickness <= 0.0)
        {
            thickness = val;
            break;
        }
    }

    if (diameter > 0.0)
        m_currentDiameter = diameter;
    if (thickness > 0.0)
        m_currentWallThickness = thickness;

    // Если позже придёт "*** Pipe OD ..." — перезапишет m_currentOD; иначе используем m_currentDiameter.
    return true;
}

bool CNTLParser::ParseSupport(const CString& line)
{
    // Формат из Test.NTL: SPRG A01 Y1 H * N 1000.00 * 0.250 0.000 0.000 BPOP None None None 1 N N N 1.000 1.000
    // SPRG <name> <type> <orientation> ... <distance> ... <coordinates> ...
    
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)
    {
        return false;
    }

    NTLSupport support;
    
    // Имя опоры (токен 1)
    if (tokens.size() > 1)
        support.name = tokens[1];
    
    // Тип опоры (токен 2, например "Y1")
    if (tokens.size() > 2)
        support.supportType = tokens[2];
    
    // Расстояние - ищем числовое значение после "N"
    // В примере: 1000.00 - это расстояние от начала
    bool foundN = false;
    for (size_t i = 3; i < tokens.size(); i++)
    {
        CString token = tokens[i];
        token.MakeUpper();
        if (token == _T("N"))
        {
            foundN = true;
            continue;
        }
        
        if (foundN)
        {
            double val = StringToDouble(tokens[i]);
            if (val > 0.0 && val < 100000.0) // Разумные пределы для расстояния в мм
            {
                support.distance = val;
                break;
            }
        }
    }
    
    // Координаты опоры - ищем три последовательных числа после второго "*"
    // В примере: 0.250 0.000 0.000 - это могут быть координаты
    int starCount = 0;
    int coordStart = -1;
    for (size_t i = 3; i < tokens.size(); i++)
    {
        if (tokens[i] == _T("*"))
        {
            starCount++;
            if (starCount >= 2) // Второй "*"
            {
                coordStart = (int)i + 1;
                break;
            }
        }
    }
    
    if (coordStart >= 0 && coordStart + 2 < (int)tokens.size())
    {
        support.position.x = StringToDouble(tokens[coordStart]);
        support.position.y = StringToDouble(tokens[coordStart + 1]);
        support.position.z = StringToDouble(tokens[coordStart + 2]);
    }
    else
    {
        // Если координаты не найдены, используем последнюю точку сегмента
        support.position = m_lastPoint;
    }
    
    m_supports.push_back(support);
    
    return true;
}

bool CNTLParser::ParseOperation(const CString& line)
{
    // Формат: OPER A00 1 70.000 0.000 29.5 * 12000.000
    // OPER <name> <param1> <temperature> <pressure> <param2> ...
    
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)
    {
        return false;
    }

    NTLOperation oper;
    
    // Имя операции (токен 1)
    if (tokens.size() > 1)
        oper.name = tokens[1];
    
    // Температура (токен 3, обычно)
    if (tokens.size() > 3)
    {
        oper.temperature = StringToDouble(tokens[3]);
    }
    
    // Давление (токен 4, обычно)
    if (tokens.size() > 4)
    {
        oper.pressure = StringToDouble(tokens[4]);
    }
    
    m_operations.push_back(oper);
    
    return true;
}

bool CNTLParser::ParseRun(const CString& line)
{
    // Формат: RUN A01 2222.000 0.000 0.000 *** Global Coordinates 2222.000 0.000 0.000
    // RUN <name> <x> <y> <z> ...
    
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 5)
    {
        return false;
    }

    // Трактуем RUN как приращение координат (dx,dy,dz) от последней точки
    AcGeVector3d delta(
        StringToDouble(tokens[2]),
        StringToDouble(tokens[3]),
        StringToDouble(tokens[4]));

    AcGePoint3d newPoint = m_lastPoint + delta;
    return CreateSegmentTo(newPoint);
}

bool CNTLParser::ParseBend(const CString& line)
{
    // Формат: BEND <name> dx dy dz ...
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 5)
    {
        return false;
    }
    AcGeVector3d delta(
        StringToDouble(tokens[2]),
        StringToDouble(tokens[3]),
        StringToDouble(tokens[4]));
    AcGePoint3d newPoint = m_lastPoint + delta;
    return CreateSegmentTo(newPoint);
}

bool CNTLParser::ParseInlineValv(const CString& line)
{
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)
        return false;
    NTLInline il;
    il.type = NTLInline::Type::Inline;
    il.name = tokens[1];
    il.segmentId = m_currentSegmentId;
    AcGeVector3d delta(0, 0, 0);
    if (tokens.size() > 4)
    {
        delta.set(
            StringToDouble(tokens[2]),
            StringToDouble(tokens[3]),
            StringToDouble(tokens[4]));
    }
    il.position = m_lastPoint + delta;
    il.distance = m_currentDistance + delta.length();
    m_inlines.push_back(il);
    return true;
}

bool CNTLParser::ParseInlineFla(const CString& line)
{
    // Фланцы не создаём как отдельные inline-элементы, пропускаем
    return true;
}

bool CNTLParser::ParseInlineRed(const CString& line)
{
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)
        return false;
    NTLInline il;
    il.type = NTLInline::Type::Reducer;
    il.name = tokens[1];
    il.segmentId = m_currentSegmentId;
    AcGeVector3d delta(0, 0, 0);
    if (tokens.size() > 4)
    {
        delta.set(
            StringToDouble(tokens[2]),
            StringToDouble(tokens[3]),
            StringToDouble(tokens[4]));
    }
    il.position = m_lastPoint + delta;
    il.distance = m_currentDistance + delta.length();
    m_inlines.push_back(il);
    return true;
}

bool CNTLParser::ParseInlineTee(const CString& line)
{
    std::vector<CString> tokens = Tokenize(line);
    if (tokens.size() < 2)
        return false;
    NTLInline il;
    il.type = NTLInline::Type::Tee;
    il.name = tokens[1];
    il.segmentId = m_currentSegmentId;
    AcGeVector3d delta(0, 0, 0);
    if (tokens.size() > 4)
    {
        delta.set(
            StringToDouble(tokens[2]),
            StringToDouble(tokens[3]),
            StringToDouble(tokens[4]));
    }
    il.position = m_lastPoint + delta;
    il.distance = m_currentDistance + delta.length();
    m_inlines.push_back(il);
    return true;
}

std::vector<CString> CNTLParser::Tokenize(const CString& line) const
{
    std::vector<CString> tokens;
    CString str = line;
    str.TrimLeft();
    str.TrimRight();
    
    int pos = 0;
    CString token = str.Tokenize(_T(" \t"), pos);
    while (!token.IsEmpty() || pos >= 0)
    {
        token.TrimLeft();
        token.TrimRight();
        if (!token.IsEmpty())
        {
            tokens.push_back(token);
        }
        if (pos < 0)
            break;
        token = str.Tokenize(_T(" \t"), pos);
    }
    
    return tokens;
}

double CNTLParser::StringToDouble(const CString& str) const
{
    if (str.IsEmpty())
        return 0.0;
    
    CString cleanStr = str;
    cleanStr.Remove(_T('*'));
    cleanStr.TrimLeft();
    cleanStr.TrimRight();
    
    if (cleanStr.IsEmpty())
        return 0.0;
    
    return _ttof(cleanStr);
}

int CNTLParser::StringToInt(const CString& str) const
{
    if (str.IsEmpty())
        return 0;
    
    CString cleanStr = str;
    cleanStr.Remove(_T('*'));
    cleanStr.TrimLeft();
    cleanStr.TrimRight();
    
    if (cleanStr.IsEmpty())
        return 0;
    
    return _ttoi(cleanStr);
}

bool CNTLParser::CreateSegmentTo(const AcGePoint3d& newPoint)
{
    NTLSegment seg;
    seg.name = m_lastSegName;
    seg.pipeName = m_currentPipeName.IsEmpty() ? (m_lastSegName + _T("_PIPE")) : m_currentPipeName;
    seg.startPoint = m_lastPoint;
    seg.endPoint = newPoint;
    seg.length = seg.startPoint.distanceTo(seg.endPoint);
    seg.diameter = (m_currentOD > 0.0) ? m_currentOD : m_currentDiameter;
    seg.wallThickness = m_currentWallThickness;
    seg.segmentId = m_currentSegmentId;
    if (seg.wallThickness <= 0.0 && seg.diameter > 0.0)
    {
        seg.wallThickness = seg.diameter * 0.1;
    }

    m_segments.push_back(seg);
    m_lastPoint = newPoint;
    m_currentDistance += seg.length;
    return true;
}
