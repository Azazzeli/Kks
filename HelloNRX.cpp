// Совместимость с Model Studio / nanoCAD
#define _ARXVER_202X
#define _ACRX_VER
#define NCAD            // nanoCAD
#define _MSC_VER 1920   // под вашу VS

// Для правильной работы с кириллицей в комментариях:
// В Visual Studio: File -> Advanced Save Options -> Encoding -> Unicode (UTF-8 with signature) - Codepage 65001
// Или используйте Windows-1251 (CP1251) для совместимости со старыми проектами
// Все строки вывода переведены на английский для избежания проблем с кодировкой

#include "stdafx.h"
#include <windows.h>
#include <tchar.h>
#include <fstream>
#include <sstream>
#include <string>
// ObjectARX / nanoCAD SDK
#include "acdb.h"
#include "dbmain.h"
#include "dbapserv.h"
#include "dbtable.h"
#include "dbents.h"
#include "geassign.h"
#include "rxregsvc.h"
#include "acgi.h"
#include "aced.h"
#include "dbxutil.h"
#include "ursUtils.h"

#include <vector>
#include <algorithm>
#include <cwctype>
#include <cwchar>

// ViperCS / Model Studio SDK - для создания трубы
#include "vCSCreatePipe.h"
#include "vCSProfileCircle.h"
#include "vCSProfileBase.h"
#include "vCSDragManager.h"
#include "vCSNode.h"
#include "vCSAxis.h"
#include "vCSSegment.h"
#include "vCSInLine.h"
#include "vCSILBase.h"
#include "vCSSupport.h"
#include "vCS_DM_Axis.h"
#include "vCS_DM_Seg.h"
#include "vCS_DM_ILBase.h"
#include "VCSUtils.h"

// Парсер NTL формата
#include "NTLParser.h"

namespace
{
// Получаем путь к лог-файлу в %TEMP%
std::wstring GetLogFilePath()
{
    static std::wstring path;
    if (!path.empty())
        return path;

    wchar_t tempPath[MAX_PATH] = { 0 };
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len > MAX_PATH)
    {
        path = L"HelloNRX.log";
    }
    else
    {
        path.assign(tempPath);
        if (!path.empty() && path.back() != L'\\')
            path.push_back(L'\\');
        path += L"HelloNRX.log";
    }
    return path;
}

// Простое логирование в файл и отладчикyfq попроб
void LogMessage(const wchar_t* fmt, ...)
{
    wchar_t message[1024] = { 0 };
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(message, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t ts[64] = { 0 };
    swprintf_s(ts, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::wstring line = std::wstring(L"[") + ts + L"] " + message + L"\n";

    try
    {
        std::wofstream log(GetLogFilePath(), std::ios::app);
        if (log)
        {
            log << line;
            log.flush();
        }
    }
    catch (...)
    {
        // Игнорируем ошибки записи логов
    }

    OutputDebugStringW(line.c_str());
}

std::wstring PointToStr(const AcGePoint3d& pt)
{
    std::wstringstream ss;
    ss << L"(" << pt.x << L"," << pt.y << L"," << pt.z << L")";
    return ss.str();
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty())
        return true;
    auto toLower = [](wchar_t c) { return (wchar_t)std::towlower(c); };
    std::wstring h, n;
    h.resize(haystack.size());
    n.resize(needle.size());
    std::transform(haystack.begin(), haystack.end(), h.begin(), toLower);
    std::transform(needle.begin(), needle.end(), n.begin(), toLower);
    return h.find(n) != std::wstring::npos;
}

// Определяем, похоже ли имя класса на dummy (ссылочный объект)
bool IsDummyClass(const std::wstring& className)
{
    return ContainsNoCase(className, L"dummy");
}

// Определяем, похоже ли на арматуру (inline/valve/armatur)
bool IsArmatureClass(const std::wstring& className)
{
    return ContainsNoCase(className, L"inline") ||
        ContainsNoCase(className, L"valve") ||
        ContainsNoCase(className, L"armatur");
}

std::wstring ExtractPartNameFromRb(resbuf* prb)
{
    for (resbuf* p = prb; p; p = p->rbnext)
    {
        if (p->restype == 1000 || p->restype == 1003 || p->restype == 1004)
        {
            if (p->resval.rstring)
            {
                std::wstring val(p->resval.rstring);
                // Если строка вида "PART_NAME=XXX"
                const std::wstring key = L"PART_NAME=";
                size_t pos = val.find(key);
                if (pos != std::wstring::npos)
                {
                    return val.substr(pos + key.size());
                }
                // Или просто содержимое, если регистрируется под своим именем
                if (ContainsNoCase(val, L"part"))
                    return val;
            }
        }
    }
    return L"";
}

// Пытаемся достать параметр KKS_PART через параметры объекта.
// hasParam = true, если параметр существует (может быть пустым).
std::wstring GetKKSPart(AcDbEntity* pEnt, bool& hasParam)
{
    hasParam = false;
    if (!pEnt)
        return L"";

    CElement params;
    if (!ursGetObjectParameters(pEnt->objectId(), params))
        return L"";

    CString kks;
    hasParam = params.GetValue(L"KKS_PART", kks);
    if (!hasParam)
        return L"";
    return std::wstring(kks.GetString());
}

// Получаем центр геометрических экстентов в WCS
AcGePoint3d GetEntityCenter(AcDbEntity* pEnt, bool& ok)
{
    ok = false;
    if (!pEnt)
        return AcGePoint3d::kOrigin;
    AcDbExtents ext;
    if (pEnt->getGeomExtents(ext) != Acad::eOk)
        return AcGePoint3d::kOrigin;
    ok = true;
    return AcGePoint3d(
        (ext.minPoint().x + ext.maxPoint().x) * 0.5,
        (ext.minPoint().y + ext.maxPoint().y) * 0.5,
        (ext.minPoint().z + ext.maxPoint().z) * 0.5);
}

struct ArmatureInfo
{
    AcDbObjectId id;
    std::wstring className;
    std::wstring kksPart;
    AcGePoint3d pos;
    bool isDummy;
};

// Экспорт таблицы арматуры в %TEMP%\ArmatureTable.csv
void exportArmatureTable()
{
    LogMessage(L"BEGIN exportArmatureTable");
    try
    {
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb)
        {
            acutPrintf(L"\nERROR: No active database.");
            LogMessage(L"exportArmatureTable: no DB");
            return;
        }

        AcDbBlockTable* pBT = nullptr;
        if (pDb->getBlockTable(pBT, AcDb::kForRead) != Acad::eOk || !pBT)
        {
            acutPrintf(L"\nERROR: Cannot get block table.");
            LogMessage(L"exportArmatureTable: BT fail");
            return;
        }

        AcDbBlockTableRecord* pMS = nullptr;
        if (pBT->getAt(ACDB_MODEL_SPACE, pMS, AcDb::kForRead) != Acad::eOk || !pMS)
        {
            acutPrintf(L"\nERROR: Cannot get model space.");
            LogMessage(L"exportArmatureTable: MS fail");
            pBT->close();
            return;
        }

        std::vector<ArmatureInfo> allArm;
        AcDbBlockTableRecordIterator* pIter = nullptr;
        if (pMS->newIterator(pIter) == Acad::eOk && pIter)
        {
            for (pIter->start(); !pIter->done(); pIter->step())
            {
                AcDbEntity* pEnt = nullptr;
                if (pIter->getEntity(pEnt, AcDb::kForRead) != Acad::eOk || !pEnt)
                    continue;

                std::wstring cls = pEnt->isA() ? pEnt->isA()->name() : L"";
                bool isDummy = IsDummyClass(cls);
                bool isArm = IsArmatureClass(cls);

                if (!isArm)
                {
                    pEnt->close();
                    continue;
                }

                bool ok = false;
                AcGePoint3d center = GetEntityCenter(pEnt, ok);
                if (!ok)
                {
                    pEnt->close();
                    continue;
                }

                bool hasKks = false;
                std::wstring kksPart = GetKKSPart(pEnt, hasKks);
                if (!hasKks)
                {
                    pEnt->close();
                    continue; // параметра нет совсем — пропускаем
                }

                ArmatureInfo info;
                info.id = pEnt->objectId();
                info.className = cls;
                info.kksPart = kksPart;
                info.pos = center;
                info.isDummy = isDummy;
                allArm.push_back(info);

                pEnt->close();
            }
            delete pIter;
        }

        pMS->close();
        pBT->close();

        if (allArm.empty())
        {
            acutPrintf(L"\nWARNING: No armature-like objects found.");
            LogMessage(L"exportArmatureTable: none found");
            return;
        }

        // Если есть dummy, используем только их; иначе все
        bool hasDummy = std::any_of(allArm.begin(), allArm.end(), [](const ArmatureInfo& i) { return i.isDummy; });
        std::vector<const ArmatureInfo*> selected;
        for (const auto& a : allArm)
        {
            if (hasDummy)
            {
                if (a.isDummy)
                    selected.push_back(&a);
            }
            else
            {
                selected.push_back(&a);
            }
        }

        if (selected.empty())
        {
            acutPrintf(L"\nWARNING: Armatures found, but none match dummy/non-dummy filter.");
            LogMessage(L"exportArmatureTable: filtered empty, hasDummy=%d", hasDummy ? 1 : 0);
            return;
        }

        // Готовим файл
        wchar_t tempPath[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring csvPath(tempPath);
        if (!csvPath.empty() && csvPath.back() != L'\\')
            csvPath.push_back(L'\\');
        csvPath += L"ArmatureTable.csv";

        std::ofstream out8(csvPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out8)
        {
            acutPrintf(L"\nERROR: Cannot reopen file: %s", csvPath.c_str());
            LogMessage(L"exportArmatureTable: reopen fail %s", csvPath.c_str());
            return;
        }
        // UTF-8 BOM
        unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        out8.write(reinterpret_cast<const char*>(bom), sizeof(bom));

        auto toUtf8 = [](const std::wstring& ws) {
            if (ws.empty()) return std::string();
            int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
            std::string res;
            res.resize(size);
            if (size > 0)
            {
                WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &res[0], size, nullptr, nullptr);
            }
            return res;
        };

        out8 << "KKS_PART;X;Y;Z;ClassName;Dummy\n";
        for (const auto* a : selected)
        {
            std::string part = toUtf8(a->kksPart);
            std::string cls = toUtf8(a->className);
            out8 << part << ";"
                << a->pos.x << ";"
                << a->pos.y << ";"
                << a->pos.z << ";"
                << cls << ";"
                << (a->isDummy ? "1" : "0") << "\n";
        }
        out8.close();

        acutPrintf(L"\nOK: Exported %d armature items to %s", (int)selected.size(), csvPath.c_str());
        LogMessage(L"exportArmatureTable: exported %d items to %s", (int)selected.size(), csvPath.c_str());
    }
    catch (const std::exception& ex)
    {
        LogMessage(L"exportArmatureTable std::exception: %hs", ex.what());
        acutPrintf(L"\nERROR: %hs", ex.what());
    }
    catch (...)
    {
        LogMessage(L"exportArmatureTable unknown error");
        acutPrintf(L"\nERROR: Unknown error in exportArmatureTable.");
    }
}

} // namespace

/**
 * Автоматическая функция для создания тестовой круглой трубы по 3 точкам с опорой и арматурой
 */
void createTestPipe()
{
    LogMessage(L"BEGIN createTestPipe");
    try
    {
        acutPrintf(L"\n=== Automatic test pipe creation with support and inline ===\n");

        // Убеждаемся, что используется круглый профиль (не прямоугольный)
        vCSDragManager* pDM = vCSDragManager::DM();
        if (pDM && pDM->getSettingsTracing())
        {
            if (pDM->getSettingsTracing()->IsRectProfile())
            {
                pDM->getSettingsTracing()->m_bRectProfile = false;
                LogMessage(L"Settings: switched to round profile");
            }
        }

        // Автоматически создаём 3 точки для трубы
        AcGePoint3d ptStart(0.0, 0.0, 0.0);
        AcGePoint3d ptMiddle(5000.0, 0.0, 0.0);
        AcGePoint3d ptEnd(5000.0, 5000.0, 0.0);

        LogMessage(L"Points: Start=%s, Middle=%s, End=%s",
            PointToStr(ptStart).c_str(),
            PointToStr(ptMiddle).c_str(),
            PointToStr(ptEnd).c_str());

        // Создаем массив точек для пути трубы
        AcGePoint3dArray path;
        path.append(ptStart);
        path.append(ptMiddle);
        path.append(ptEnd);

        // Создаем круглый профиль трубы (диаметр 108 мм, DN 100)
        vCSProfilePtr pProfile = new vCSProfileCircle(true, 108.0, 100.0);
        if (!pProfile)
        {
            acutPrintf(L"\nERROR: Failed to create pipe profile.");
            LogMessage(L"Error creating vCSProfileCircle");
            return;
        }
        LogMessage(L"Created round profile: diameter=108.0, DN=100.0");

        // Создаем трубу с круглым профилем по трем точкам
        // ВНИМАНИЕ: idLCSN_Start и idLCSN_End передаются ПО ЗНАЧЕНИЮ, не по ссылке
        // Метод CalculateAndConnect НЕ изменяет эти параметры - они используются для ПОДКЛЮЧЕНИЯ к существующим узлам
        // Для создания новой трубы передаем kNull, что означает создание новых узлов
        AcDbObjectId idLCSN_Start = AcDbObjectId::kNull;
        AcDbObjectId idLCSN_End = AcDbObjectId::kNull;
        
        CreatePipeOnPointsWithProfiles cpop(path, pProfile, pProfile, pProfile);

        LogMessage(L"Calling CreatePipeOnPointsWithProfiles");
        
        Acad::ErrorStatus es = cpop.CalculateAndConnect(idLCSN_Start, idLCSN_End);
        if (es == Acad::eInvalidOffset || es != Acad::eOk)
        {
            acutPrintf(L"\nERROR: Failed to create pipe (code: %d).", es);
            LogMessage(L"Error creating pipe, code: %d", es);
            return;
        }

        LogMessage(L"Pipe created successfully (code: %d). Input Start node ID: %ld, End node ID: %ld", 
            es, idLCSN_Start.asOldId(), idLCSN_End.asOldId());

        // Получаем ID созданной оси через getCalculatedAxis (основной способ)
        // Этот метод должен вернуть ось, которая была создана во время CalculateAndConnect
        AcDbObjectId pipeAxisId = AcDbObjectId::kNull;
        vCS_DM_Axis* pDM_Axis = cpop.getCalculatedAxis();
        if (pDM_Axis)
        {
            pipeAxisId = pDM_Axis->OID();
            LogMessage(L"Got axis ID from getCalculatedAxis: %ld", pipeAxisId.asOldId());
        }
        else
        {
                LogMessage(L"WARNING: getCalculatedAxis() returned nullptr, trying alternative methods");
            
            // Альтернативный способ 1: получаем ось через Drag Manager (если он активен)
            vCSDragManager* pDM = vCSDragManager::DM();
            if (pDM)
            {
                // Пробуем получить ID созданных осей из Drag Manager
                AcDbObjectIdArray arrNewAxises;
                pDM->GetOIdNewAxises(arrNewAxises);
                if (arrNewAxises.length() > 0)
                {
                    pipeAxisId = arrNewAxises[0];
                    LogMessage(L"Got axis ID from Drag Manager GetOIdNewAxises: %ld", pipeAxisId.asOldId());
                }
            }
            
            // Альтернативный способ 2: пробуем найти ось через поиск сегментов в базе данных
            // Ищем все сегменты (vCSSegment2) и получаем ось из последнего созданного (скорее всего это наш)
            if (pipeAxisId.isNull())
            {
                try
                {
                    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
                    if (pDb)
                    {
                        AcDbBlockTable* pBlockTable = nullptr;
                        if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) == Acad::eOk)
                        {
                            AcDbBlockTableRecord* pModelSpace = nullptr;
                            if (pBlockTable->getAt(ACDB_MODEL_SPACE, pModelSpace, AcDb::kForRead) == Acad::eOk)
                            {
                                AcDbBlockTableRecordIterator* pIter = nullptr;
                                if (pModelSpace->newIterator(pIter) == Acad::eOk)
                                {
                                    // Ищем последний созданный сегмент (он скорее всего наш)
                                    AcDbObjectId lastSegAxisId = AcDbObjectId::kNull;
                                    for (pIter->start(); !pIter->done(); pIter->step())
                                    {
                                        AcDbEntity* pEnt = nullptr;
                                        if (pIter->getEntity(pEnt, AcDb::kForRead) == Acad::eOk)
                                        {
                                            // Пробуем привести к vCSSegment2
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
                                    
                                    if (!lastSegAxisId.isNull())
                                    {
                                        pipeAxisId = lastSegAxisId;
                                        LogMessage(L"Found axis ID from last segment in database: %ld", pipeAxisId.asOldId());
                                    }
                                }
                                pModelSpace->close();
                            }
                            pBlockTable->close();
                        }
                    }
                }
                catch (...)
                {
                    LogMessage(L"Exception during database search for axis");
                }
            }
        }

        if (pipeAxisId.isNull())
        {
            acutPrintf(L"\nERROR: Failed to get pipe axis.");
            LogMessage(L"Failed to get pipeAxisId");
            return;
        }

        acutPrintf(L"\nOK: Pipe created successfully! Axis ID: %ld", pipeAxisId.asOldId());
        LogMessage(L"Pipe created, axis ID: %ld", pipeAxisId.asOldId());

        // Теперь добавляем опору и арматуру на трубу
        // Используем Drag Manager для работы с осью (как в примере из SDK)
        pDM->End();
        pDM->Clear();

        if (!pDM->setAcGsViewForViewPort(true))
        {
            acutPrintf(L"\nWARNING: Failed to set AcGsView");
            LogMessage(L"Warning: failed to set AcGsView");
        }

        pDM->Start(pipeAxisId);
        vCS_DM_Axis* pAxis = pDM->GetAxis(pipeAxisId);
        if (!pAxis)
        {
            acutPrintf(L"\nERROR: Failed to get axis for adding elements.");
            LogMessage(L"Error: pAxis == nullptr");
            pDM->End();
            return;
        }

        if (pAxis->GetSegCount() == 0)
        {
            acutPrintf(L"\nERROR: No segments on pipe.");
            LogMessage(L"Error: no segments");
            pDM->End();
            return;
        }

        // Получаем первый сегмент по индексу
        vCS_DM_Seg* pSeg = pAxis->GetSeg(0);
        if (!pSeg)
        {
            acutPrintf(L"\nERROR: Failed to get segment.");
            LogMessage(L"Error: pSeg == nullptr");
            pDM->End();
            return;
        }

        // Получаем точки сегмента для вычисления расстояний
        AcGePoint3d segStart = pSeg->GetStartPoint();
        AcGePoint3d segEnd = pSeg->GetEndPoint();
        double segLength = segStart.distanceTo(segEnd);

        LogMessage(L"Segment: length=%.2f, start=%s, end=%s", 
            segLength, PointToStr(segStart).c_str(), PointToStr(segEnd).c_str());

        // Добавляем опору на 30% длины сегмента от начала
        double supportOffset = segLength * 0.3;
        LogMessage(L"Adding support at distance %.2f from start", supportOffset);
        
        vCS_DM_Support* pSupport = pSeg->AddSupport(supportOffset, st_Support);
        if (pSupport)
        {
            pSupport->SetDMAxis(pAxis);
            pSupport->SetSeg(pSeg);
            
            // Вычисляем точку для базовой позиции опоры
            AcGeVector3d segDir = (segEnd - segStart).normal();
            AcGePoint3d supportPoint = segStart + segDir * supportOffset;
            pSupport->SetBasePoint(supportPoint);
            
            LogMessage(L"Support created, ID: %ld", pSupport->OID().asOldId());
            acutPrintf(L"\nOK: Support added at distance %.2f mm", supportOffset);
        }
        else
        {
            LogMessage(L"Warning: failed to create support");
            acutPrintf(L"\nWARNING: Failed to create support");
        }

        // Добавляем арматуру на 70% длины сегмента от начала
        double inlineOffset = segLength * 0.7;
        LogMessage(L"Adding inline at distance %.2f from start", inlineOffset);
        
        vCS_DM_InLine* pInline = pSeg->AddInLine(inlineOffset, static_cast<unsigned int>(vCSILBase::til_inline));
        if (pInline)
        {
            pInline->SetDMAxis(pAxis);
            pInline->SetSeg(pSeg);
            
            // Вычисляем точку для базовой позиции арматуры
            AcGeVector3d segDir = (segEnd - segStart).normal();
            AcGePoint3d inlinePoint = segStart + segDir * inlineOffset;
            pInline->SetBasePoint(inlinePoint);
            
            LogMessage(L"Inline created, ID: %ld", pInline->OID().asOldId());
            acutPrintf(L"\nOK: Inline added at distance %.2f mm", inlineOffset);
        }
        else
        {
            LogMessage(L"Warning: failed to create inline");
            acutPrintf(L"\nWARNING: Failed to create inline");
        }

        // Добавляем переход (reducer) на 40% длины сегмента от начала
        double reducerOffset = segLength * 0.4;
        LogMessage(L"Adding reducer at distance %.2f from start", reducerOffset);
        
        vCS_DM_InLine* pReducer = pSeg->AddInLine(reducerOffset, static_cast<unsigned int>(vCSILBase::til_reducer));
        if (pReducer)
        {
            pReducer->SetDMAxis(pAxis);
            pReducer->SetSeg(pSeg);
            
            // Устанавливаем тип как переход
            pReducer->SetIsReducer(true);
            
            // Вычисляем точку для базовой позиции перехода
            AcGeVector3d segDir = (segEnd - segStart).normal();
            AcGePoint3d reducerPoint = segStart + segDir * reducerOffset;
            pReducer->SetBasePoint(reducerPoint);
            
            LogMessage(L"Reducer created, ID: %ld", pReducer->OID().asOldId());
            acutPrintf(L"\nOK: Reducer added at distance %.2f mm", reducerOffset);
        }
        else
        {
            LogMessage(L"Warning: failed to create reducer");
            acutPrintf(L"\nWARNING: Failed to create reducer");
        }

        // Добавляем тройник (tee) на 60% длины сегмента от начала
        double teeOffset = segLength * 0.6;
        LogMessage(L"Adding tee at distance %.2f from start", teeOffset);
        
        vCS_DM_InLine* pTee = pSeg->AddInLine(teeOffset, static_cast<unsigned int>(vCSILBase::til_tee));
        if (pTee)
        {
            pTee->SetDMAxis(pAxis);
            pTee->SetSeg(pSeg);
            
            // Устанавливаем тип как тройник
            pTee->SetIsTee(true);
            
            // Вычисляем точку для базовой позиции тройника
            AcGeVector3d segDir = (segEnd - segStart).normal();
            AcGePoint3d teePoint = segStart + segDir * teeOffset;
            pTee->SetBasePoint(teePoint);
            
            LogMessage(L"Tee created, ID: %ld", pTee->OID().asOldId());
            acutPrintf(L"\nOK: Tee added at distance %.2f mm", teeOffset);
        }
        else
        {
            LogMessage(L"Warning: failed to create tee");
            acutPrintf(L"\nWARNING: Failed to create tee");
        }

        // Пересчитываем модель и обновляем базу данных (как в примере из SDK)
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

        acutPrintf(L"\nOK: Done! Pipe created with support, inline, reducer and tee.");
        LogMessage(L"END createTestPipe - success");
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
}

/**
 * Функция импорта трубы из NTL файла
 */
void importFromNTL()
{
    LogMessage(L"BEGIN importFromNTL");
    try
    {
        acutPrintf(L"\n=== Import pipe from NTL file ===\n");

        // Запрашиваем путь к NTL файлу
        struct resbuf resultBuf;
        memset(&resultBuf, 0, sizeof(resultBuf));

        LogMessage(L"importFromNTL: before file dialog");
        int result = acedGetFileD(
            _T("Select NTL file to import"), // title
            nullptr,                         // default name
            _T("ntl"),                       // extension
            0,                               // flags
            &resultBuf);
        LogMessage(L"importFromNTL: after file dialog, result=%d, restype=%d", result, resultBuf.restype);

        bool hasString = (resultBuf.restype == RTSTR && resultBuf.resval.rstring != nullptr);

        if (result != RTNORM || !hasString)
        {
            acutPrintf(L"\nImport cancelled or file not selected.");
            LogMessage(L"Import cancelled or no file selected (result=%d, restype=%d)", result, resultBuf.restype);
            if (hasString)
                acutRelRb(&resultBuf);
            return;
        }

        // Копируем путь из resbuf в локальный буфер с проверками
        if (!resultBuf.resval.rstring)
        {
            acutPrintf(L"\nERROR: Invalid file path from dialog.");
            LogMessage(L"ERROR: resultBuf.resval.rstring is null");
            acutRelRb(&resultBuf);
            return;
        }

        wchar_t pathBufLocal[MAX_PATH * 4] = { 0 };
        wcsncpy_s(pathBufLocal, resultBuf.resval.rstring, _TRUNCATE);
        if (pathBufLocal[0] == L'\0')
        {
            acutPrintf(L"\nERROR: Empty file path from dialog.");
            LogMessage(L"ERROR: Empty file path after copy");
            acutRelRb(&resultBuf);
            return;
        }

        CString filePath(pathBufLocal);
        LogMessage(L"importFromNTL: selected path='%s'", filePath.GetString());
        // Не вызываем acutRelRb здесь, чтобы исключить потенциальный краш в диалоге

        // Проверяем существование файла
        int accessRes = _waccess(filePath, 0);
        LogMessage(L"importFromNTL: _waccess result=%d", accessRes);
        if (accessRes != 0)
        {
            acutPrintf(L"\nERROR: File does not exist: %s", filePath.GetString());
            LogMessage(L"ERROR: File does not exist: %s", filePath.GetString());
            return;
        }

        // Создаем парсер и читаем файл
        CNTLParser parser;
        LogMessage(L"importFromNTL: before parser.ReadFile");
        bool parseOk = false;
        try
        {
            parseOk = parser.ReadFile(filePath);
        }
        catch (const std::exception& ex)
        {
            LogMessage(L"importFromNTL: exception in parser.ReadFile: %hs", ex.what());
            parseOk = false;
        }
        catch (...)
        {
            LogMessage(L"importFromNTL: unknown exception in parser.ReadFile");
            parseOk = false;
        }
        if (!parseOk)
        {
            acutPrintf(L"\nERROR: Failed to read NTL file: %s", filePath.GetString());
            LogMessage(L"ERROR: Failed to read NTL file: %s", filePath.GetString());
            return;
        }
        LogMessage(L"importFromNTL: parser.ReadFile OK");

        // Получаем сегменты из парсера
        const std::vector<NTLSegment>& segmentsParsed = parser.GetSegments();
        if (segmentsParsed.empty())
        {
            acutPrintf(L"\nWARNING: No segments found in NTL file.");
            LogMessage(L"WARNING: No segments found");
            return;
        }

        // Предобработка: удаляем нулевые и склеиваем коллинеарные последовательные отрезки с одинаковым OD/WT/pipe/segmentId
        std::vector<NTLSegment> segments;
        segments.reserve(segmentsParsed.size());
        auto sameDir = [](const AcGeVector3d& a, const AcGeVector3d& b)
        {
            double la = a.length(); double lb = b.length();
            if (la < 1e-6 || lb < 1e-6) return false;
            AcGeVector3d na = a / la; AcGeVector3d nb = b / lb;
            double dot = na.dotProduct(nb);
            return fabs(dot - 1.0) < 1e-3; // почти параллельны
        };
        for (const auto& s : segmentsParsed)
        {
            double len = s.startPoint.distanceTo(s.endPoint);
            if (len < 1e-6)
            {
                LogMessage(L"Skip zero-length segment %s", s.name.GetString());
                continue;
            }
            if (!segments.empty())
            {
                NTLSegment& last = segments.back();
                bool sameMeta =
                    last.segmentId.CompareNoCase(s.segmentId) == 0 &&
                    fabs(last.diameter - s.diameter) < 1e-6 &&
                    fabs(last.wallThickness - s.wallThickness) < 1e-6 &&
                    last.endPoint.distanceTo(s.startPoint) < 1e-3;
                if (sameMeta)
                {
                    AcGeVector3d d1 = last.endPoint - last.startPoint;
                    AcGeVector3d d2 = s.endPoint - s.startPoint;
                    if (sameDir(d1, d2))
                    {
                        last.endPoint = s.endPoint;
                        last.length += len;
                        LogMessage(L"Merged collinear segment %s into %s, new len=%.3f", s.name.GetString(), last.name.GetString(), last.length);
                        continue;
                    }
                }
            }
            segments.push_back(s);
        }

        acutPrintf(L"\nFound %d segments in NTL file (merged %d -> %d)", (int)segmentsParsed.size(), (int)segmentsParsed.size(), (int)segments.size());
        LogMessage(L"Found %d segments raw, after merge %d", (int)segmentsParsed.size(), (int)segments.size());

        // Убеждаемся, что используется круглый профиль
        vCSDragManager* pDM = vCSDragManager::DM();
        if (!pDM)
        {
            acutPrintf(L"\nERROR: DragManager is null. Cannot proceed.");
            LogMessage(L"ERROR: vCSDragManager::DM() returned nullptr");
            return;
        }
        if (pDM->getSettingsTracing() && pDM->getSettingsTracing()->IsRectProfile())
        {
            pDM->getSettingsTracing()->m_bRectProfile = false;
            LogMessage(L"Settings: switched to round profile");
        }

        // Группируем непрерывные отрезки с одинаковыми OD/WT/pipeName в цепочки
        struct Chain
        {
            std::vector<const NTLSegment*> segs;
            double totalLen = 0.0;
        };

        auto samePipe = [](const NTLSegment& a, const NTLSegment& b)
        {
            return fabs(a.diameter - b.diameter) < 1e-6 &&
                fabs(a.wallThickness - b.wallThickness) < 1e-6 &&
                a.pipeName == b.pipeName;
        };

        std::vector<Chain> chains;
        if (!segments.empty())
        {
            Chain cur;
            cur.segs.push_back(&segments[0]);
            cur.totalLen = segments[0].length;
            for (size_t i = 1; i < segments.size(); ++i)
            {
                const NTLSegment& prev = *cur.segs.back();
                const NTLSegment& seg = segments[i];
                bool contiguous = prev.endPoint.distanceTo(seg.startPoint) < 1e-3;
                if (contiguous && samePipe(prev, seg))
                {
                    cur.segs.push_back(&seg);
                    cur.totalLen += seg.length;
                }
                else
                {
                    chains.push_back(cur);
                    cur = Chain{};
                    cur.segs.push_back(&seg);
                    cur.totalLen = seg.length;
                }
            }
            chains.push_back(cur);
        }

        acutPrintf(L"\nGrouped into %d continuous pipes", (int)chains.size());
        LogMessage(L"Grouped into %d chains", (int)chains.size());

        int successCount = 0;
        std::vector<AcDbObjectId> createdAxisIds; // ID осей по цепочкам

        // Создаем трубы по цепочкам
        for (size_t c = 0; c < chains.size(); ++c)
        {
            const Chain& ch = chains[c];
            if (ch.segs.empty())
                continue;
            const NTLSegment* first = ch.segs.front();
            double od = first->diameter;
            double wt = first->wallThickness;
            if (od <= 0.0)
            {
                LogMessage(L"WARNING: Chain %d invalid od, skip", (int)c);
                continue;
            }
            double dn = od - 2.0 * wt;
            if (dn <= 0.0)
                dn = od * 0.9;

            AcGePoint3dArray path;
            path.append(first->startPoint);
            for (const auto* s : ch.segs)
            {
                if (path.isEmpty() || path.last() != s->endPoint)
                    path.append(s->endPoint);
            }
            if (path.length() < 2)
                continue;

            vCSProfilePtr pProfile = new vCSProfileCircle(true, od, dn);
            if (!pProfile)
            {
                LogMessage(L"ERROR: profile null, chain %d", (int)c);
                continue;
            }

            AcDbObjectId idStart = AcDbObjectId::kNull;
            AcDbObjectId idEnd = AcDbObjectId::kNull;
            CreatePipeOnPointsWithProfiles cpop(path, pProfile, pProfile, pProfile);
            Acad::ErrorStatus es = cpop.CalculateAndConnect(idStart, idEnd);
            if (es != Acad::eOk)
            {
                LogMessage(L"ERROR: chain %d create pipe es=%d", (int)c, es);
                continue;
            }

            AcDbObjectId axisId = AcDbObjectId::kNull;
            if (auto* dmAxis = cpop.getCalculatedAxis())
                axisId = dmAxis->OID();
            if (axisId.isNull())
            {
                AcDbObjectIdArray arr;
                pDM->GetOIdNewAxises(arr);
                if (arr.length() > 0)
                    axisId = arr[arr.length() - 1];
            }
            if (axisId.isNull())
            {
                LogMessage(L"WARNING: chain %d axis null", (int)c);
                continue;
            }
            createdAxisIds.push_back(axisId);
            successCount++;
            LogMessage(L"Chain %d: pipe created, axis=%ld, od=%.3f, wt=%.3f, pts=%d",
                (int)c, axisId.asOldId(), od, wt, (int)path.length());
            acutPrintf(L"\nOK: Pipe created (chain %d) od=%.3f wt=%.3f pts=%d",
                (int)c, od, wt, (int)path.length());
        }

        // Пересчитываем модель после создания труб
        pDM->End();
        pDM->CheckForErase();
        pDM->UpdateDBEnt();
        // Добавляем опоры и инлайны по цепочкам, один пересчёт на цепочку
        const std::vector<NTLSupport>& supports = parser.GetSupports();
        const std::vector<NTLInline>& inlines = parser.GetInlines();
        acutPrintf(L"\nSupports parsed: %d, inlines parsed: %d", (int)supports.size(), (int)inlines.size());
        LogMessage(L"Parsed supports=%d, inlines=%d", (int)supports.size(), (int)inlines.size());
        int totalSupports = 0;
        int totalInlines = 0;

        for (size_t ci = 0; ci < chains.size() && ci < createdAxisIds.size(); ++ci)
        {
            const Chain& ch = chains[ci];
            if (ch.segs.empty())
                continue;

            struct SegAccum { double len; const NTLSegment* seg; };
            std::vector<SegAccum> acc;
            double total = 0.0;
            for (auto* s : ch.segs)
            {
                total += s->length;
                acc.push_back({ total, s });
            }
            if (total < 1e-6)
                continue;

            // Лог по цепочке
            if (!ch.segs.empty())
            {
                const auto* firstSeg = ch.segs.front();
                const auto* lastSeg = ch.segs.back();
                LogMessage(L"Chain %d summary: segId=%s pipe=%s od=%.3f wt=%.3f pts=%d start(%.3f,%.3f,%.3f) end(%.3f,%.3f,%.3f)",
                    (int)ci,
                    firstSeg->segmentId.GetString(),
                    firstSeg->pipeName.GetString(),
                    firstSeg->diameter,
                    firstSeg->wallThickness,
                    (int)ch.segs.size() + 1,
                    firstSeg->startPoint.x, firstSeg->startPoint.y, firstSeg->startPoint.z,
                    lastSeg->endPoint.x, lastSeg->endPoint.y, lastSeg->endPoint.z);
            }

            auto findSegAndOffset = [&](double dist, size_t& segIdx, double& localOffset)
            {
                segIdx = 0;
                double prev = 0.0;
                for (; segIdx < acc.size(); ++segIdx)
                {
                    if (dist <= acc[segIdx].len + 1e-9)
                        break;
                    prev = acc[segIdx].len;
                }
                if (segIdx >= acc.size())
                {
                    segIdx = acc.size() - 1;
                    prev = (segIdx > 0) ? acc[segIdx - 1].len : 0.0;
                }
                localOffset = dist - prev;
            };

            AcDbObjectId axisId = createdAxisIds[ci];
            pDM->End();
            pDM->Clear();
            if (!pDM->setAcGsViewForViewPort(true))
                continue;
            pDM->Start(axisId);
            vCS_DM_Axis* pAxis = pDM->GetAxis(axisId);
            if (!pAxis || pAxis->GetSegCount() == 0)
            {
                pDM->End();
                continue;
            }

            std::vector<double> usedSupportOffsets;
            std::vector<double> usedInlineOffsets;

            // Полилиния оси для проекции
            std::vector<AcGePoint3d> pts;
            pts.reserve(ch.segs.size() + 1);
            pts.push_back(ch.segs.front()->startPoint);
            for (auto* s : ch.segs)
                pts.push_back(s->endPoint);
            auto projectOnChain = [&](const AcGePoint3d& p, double& outDist, size_t& segIdx, double& localOffset)
            {
                outDist = 0.0;
                double best = 1e300;
                double accLen = 0.0;
                segIdx = 0;
                localOffset = 0.0;
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                {
                    AcGePoint3d a = pts[i];
                    AcGePoint3d b = pts[i + 1];
                    AcGeVector3d ab = b - a;
                    double len = ab.length();
                    if (len < 1e-9)
                        continue;
                    AcGeVector3d ap = p - a;
                    double t = ap.dotProduct(ab) / (len * len);
                    if (t < 0.0) t = 0.0;
                    if (t > 1.0) t = 1.0;
                    AcGePoint3d proj = a + ab * t;
                    double d2 = (proj - p).lengthSqrd();
                    if (d2 < best)
                    {
                        best = d2;
                        segIdx = i;
                        localOffset = t * len;
                        outDist = accLen + localOffset;
                    }
                    accLen += len;
                }
            };

            // Опоры
            for (const auto& sup : supports)
            {
                double dist = sup.distance;
                bool segMatch = false;
                // Пробуем по segmentId, если заполнен
                if (!sup.name.IsEmpty())
                {
                    for (auto* s : ch.segs)
                    {
                        if (!s->segmentId.IsEmpty() && sup.name.Find(s->segmentId) >= 0)
                        {
                            segMatch = true;
                            break;
                        }
                    }
                }
                // Если segmentId не помог — считаем, что опора принадлежит ближайшей цепи
                if (!segMatch)
                    segMatch = true;

                // Если расстояние некорректно — проецируем позицию
                if (dist <= 0.0 || dist > total)
                {
                    double projDist = 0.0, localOff = 0.0; size_t segProj = 0;
                    projectOnChain(sup.position, projDist, segProj, localOff);
                    dist = projDist;
                    LogMessage(L"Support %s on chain %d: use projected dist=%.3f (pos)", sup.name.GetString(), (int)ci, dist);
                }
                if (dist <= 0.0 || dist > total)
                {
                    LogMessage(L"Skip support %s on chain %d: invalid dist=%.3f (total=%.3f)", sup.name.GetString(), (int)ci, dist, total);
                    continue;
                }
                size_t segIdx = 0;
                double local = 0.0;
                findSegAndOffset(dist, segIdx, local);
                int dmSegIdx = (int)segIdx;
                if (dmSegIdx >= pAxis->GetSegCount())
                    dmSegIdx = pAxis->GetSegCount() - 1;
                vCS_DM_Seg* pSeg = pAxis->GetSeg(dmSegIdx);
                if (!pSeg)
                {
                    LogMessage(L"Skip support %s on chain %d: pSeg null", sup.name.GetString(), (int)ci);
                    continue;
                }
                double dmSegLen = pSeg->GetStartPoint().distanceTo(pSeg->GetEndPoint());
                if (dmSegLen < 1e-6 || local < 0.0 || local > dmSegLen)
                {
                    LogMessage(L"Skip support %s on chain %d: segLen=%.3f local=%.3f", sup.name.GetString(), (int)ci, dmSegLen, local);
                    continue;
                }

                // Пропускаем дубли по смещению
                bool dupSup = false;
                for (double used : usedSupportOffsets)
                    if (fabs(used - local) < 1e-3) { dupSup = true; break; }
                if (dupSup)
                {
                    LogMessage(L"Skip support %s on chain %d: duplicate offset=%.3f", sup.name.GetString(), (int)ci, local);
                    continue;
                }

                vCS_DM_Support* pSupport = pSeg->AddSupport(local, st_Support);
                if (pSupport)
                {
                    pSupport->SetDMAxis(pAxis);
                    pSupport->SetSeg(pSeg);
                    AcGeVector3d dir = (pSeg->GetEndPoint() - pSeg->GetStartPoint()).normal();
                    AcGePoint3d base = pSeg->GetStartPoint() + dir * local;
                    pSupport->SetBasePoint(base);
                    usedSupportOffsets.push_back(local);
                    totalSupports++;
                    LogMessage(L"Support %s created on chain %d seg=%d offset=%.3f base(%.3f,%.3f,%.3f)",
                        sup.name.GetString(), (int)ci, dmSegIdx, local, base.x, base.y, base.z);
                }
                else
                {
                    LogMessage(L"Support %s FAILED create on chain %d seg=%d offset=%.3f", sup.name.GetString(), (int)ci, dmSegIdx, local);
                }
            }

            // Инлайны
            for (const auto& il : inlines)
            {
                // Фильтр по segmentId, если задан: если совпадает — отлично, если нет — пробуем по вхождению имени
                if (!il.segmentId.IsEmpty())
                {
                    bool segMatch = false;
                    for (auto* s : ch.segs)
                    {
                        if (il.segmentId.CompareNoCase(s->segmentId) == 0 || il.segmentId.Find(s->segmentId) >= 0 || s->segmentId.Find(il.segmentId) >= 0)
                        {
                            segMatch = true;
                            break;
                        }
                    }
                    if (!segMatch)
                    {
                        // допускаем, но логируем
                        LogMessage(L"Inline %s on chain %d: segmentId mismatch (%s), placing by projection", il.name.GetString(), (int)ci, il.segmentId.GetString());
                    }
                }

                double dist = il.distance;
                // Если расстояние некорректно — проецируем позицию
                if (dist <= 0.0 || dist > total)
                {
                    double projDist = 0.0, localOff = 0.0; size_t segProj = 0;
                    projectOnChain(il.position, projDist, segProj, localOff);
                    dist = projDist;
                    LogMessage(L"Inline %s on chain %d: use projected dist=%.3f (pos) type=%d", il.name.GetString(), (int)ci, dist, (int)il.type);
                }
                if (dist <= 0.0 || dist > total)
                {
                    LogMessage(L"Skip inline %s on chain %d: invalid dist=%.3f (total=%.3f)", il.name.GetString(), (int)ci, dist, total);
                    continue;
                }
                size_t segIdx = 0;
                double local = 0.0;
                findSegAndOffset(dist, segIdx, local);
                int dmSegIdx = (int)segIdx;
                if (dmSegIdx >= pAxis->GetSegCount())
                    dmSegIdx = pAxis->GetSegCount() - 1;
                vCS_DM_Seg* pSeg = pAxis->GetSeg(dmSegIdx);
                if (!pSeg)
                {
                    LogMessage(L"Skip inline %s on chain %d: pSeg null", il.name.GetString(), (int)ci);
                    continue;
                }
                double dmSegLen = pSeg->GetStartPoint().distanceTo(pSeg->GetEndPoint());
                if (dmSegLen < 1e-6 || local < 0.0 || local > dmSegLen)
                {
                    LogMessage(L"Skip inline %s on chain %d: segLen=%.3f local=%.3f", il.name.GetString(), (int)ci, dmSegLen, local);
                    continue;
                }

                // Убираем дубль инлайна в одной точке
                bool dupInline = false;
                for (double used : usedInlineOffsets)
                {
                    if (fabs(used - local) < 1e-3)
                    {
                        dupInline = true;
                        break;
                    }
                }
                if (dupInline)
                {
                    LogMessage(L"Skip inline %s on chain %d: duplicate offset=%.3f", il.name.GetString(), (int)ci, local);
                    continue;
                }

                vCS_DM_InLine* pIL = nullptr;
                switch (il.type)
                {
                case NTLInline::Type::Reducer:
                    pIL = pSeg->AddInLine(local, (unsigned int)vCSILBase::til_reducer);
                    if (pIL) pIL->SetIsReducer(true);
                    break;
                case NTLInline::Type::Tee:
                    pIL = pSeg->AddInLine(local, (unsigned int)vCSILBase::til_tee);
                    if (pIL) pIL->SetIsTee(true);
                    break;
                case NTLInline::Type::Inline:
                default:
                    pIL = pSeg->AddInLine(local, (unsigned int)vCSILBase::til_inline);
                    break;
                }
                if (pIL)
                {
                    pIL->SetDMAxis(pAxis);
                    pIL->SetSeg(pSeg);
                    AcGeVector3d dir = (pSeg->GetEndPoint() - pSeg->GetStartPoint()).normal();
                    AcGePoint3d base = pSeg->GetStartPoint() + dir * local;
                    pIL->SetBasePoint(base);
                    usedInlineOffsets.push_back(local);
                    totalInlines++;
                    LogMessage(L"Inline %s created on chain %d seg=%d offset=%.3f type=%d base(%.3f,%.3f,%.3f)",
                        il.name.GetString(), (int)ci, dmSegIdx, local, (int)il.type, base.x, base.y, base.z);
                }
                else
                {
                    LogMessage(L"Inline %s FAILED create on chain %d seg=%d offset=%.3f type=%d", il.name.GetString(), (int)ci, dmSegIdx, local, (int)il.type);
                }
            }

            // Один пересчёт на цепочку
            if (totalSupports > 0 || totalInlines > 0)
            {
                pDM->ArrPtrEditableAxis_Add(pAxis);
                pDM->SetDragType(vCS::eReCalculate);
                Acad::ErrorStatus calcStatus = pDM->ReCalculateModelMain();
                if (calcStatus == Acad::eOk)
                {
                    pDM->End();
                    pDM->CheckForErase();
                    pDM->UpdateDBEnt();
                }
                else
                {
                    pDM->End();
                }
            }
            else
            {
                pDM->End();
            }
        }

        if (totalSupports > 0)
            acutPrintf(L"\nOK: Added %d supports", totalSupports);
        if (totalInlines > 0)
            acutPrintf(L"\nOK: Added %d inlines (valves/reducers/tees)", totalInlines);

        pDM->Clear();

        acutPrintf(L"\nOK: Import completed. Created %d pipes from %d chains.", successCount, (int)chains.size());
        LogMessage(L"END importFromNTL - success: created %d pipes", successCount);
    }
    catch (const std::exception& ex)
    {
        LogMessage(L"std::exception in importFromNTL: %hs", ex.what());
        acutPrintf(L"\nERROR: %hs", ex.what());
    }
    catch (...)
    {
        LogMessage(L"Unknown error in importFromNTL");
        acutPrintf(L"\nERROR: Unknown error occurred during import.");
    }
}

// -------- Точка входа nanoCAD --------
extern "C" __declspec(dllexport) AcRx::AppRetCode ncrxEntryPoint(AcRx::AppMsgCode msg, void* appId)
{
    switch (msg)
    {
    case AcRx::kInitAppMsg:
        acrxDynamicLinker->unlockApplication(appId);
        acrxDynamicLinker->registerAppMDIAware(appId);

        // Регистрируем команду для создания тестовой трубы
        acedRegCmds->addCommand(L"PIPE_TEST_GROUP",
            L"_CREATETESTPIPE", L"CREATETESTPIPE",
            ACRX_CMD_MODAL, createTestPipe);

        // Регистрируем команду для импорта из NTL файла
        acedRegCmds->addCommand(L"PIPE_TEST_GROUP",
            L"_IMPORTNTL", L"IMPORTNTL",
            ACRX_CMD_MODAL, importFromNTL);

        // Регистрируем команду для экспорта арматуры в CSV
        acedRegCmds->addCommand(L"PIPE_TEST_GROUP",
            L"_EXPORTARMATURE", L"EXPORTARMATURE",
            ACRX_CMD_MODAL, exportArmatureTable);
        break;

    case AcRx::kUnloadAppMsg:
        acedRegCmds->removeGroup(L"PIPE_TEST_GROUP");
        break;
    }
    return AcRx::kRetOK;
}
