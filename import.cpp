// pcf_import.cpp
#define _ARXVER_202X
#define _ACRX_VER
#define NCAD
#define _MSC_VER 1920

#include "stdafx.h"
#include <windows.h>
#include <tchar.h>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>

// ObjectARX / nanoCAD
#include "acdb.h"
#include "dbmain.h"
#include "dbapserv.h"
#include "geassign.h"
#include "rxregsvc.h"
#include "acgi.h"
#include "aced.h"

// ViperCS / Model Studio
#include "vCSCreatePipe.h"
#include "vCSUtils.h"
#include "..\ViperCSObj\vCSTools.h"
#include "..\ViperCSObj\vCSILBase.h"
#include "..\ViperCSObj\vCSDragManager.h"
#include "..\ViperCSObj\vCSDragManagerSmart.h"
#include "..\ViperCSObj\vCSSupport.h"
#include "..\ViperCSObj\vCS_DM_Axis.h"
#include "..\ViperCSObj\vCS_DM_Seg.h"
#include "..\ViperCSObj\vCSSettingsTracingObj.h"

namespace {

    // --------- PCF structures ----------
    struct PcfPipe { int id{}; AcGePoint3d p1{}, p2{}; double dia{ 0.0 }; };
    struct PcfElbow { int id{}; AcGePoint3d p1{}, p2{}, center{}; double dia{ 0.0 }; double angleDeg{ 0.0 }; };
    struct PcfValve { int id{}; AcGePoint3d p1{}, p2{}, center{}; double dia{ 0.0 }; };
    struct PcfSupport { int id{}; AcGePoint3d pt{}; };

    struct PcfData {
        std::vector<PcfPipe> pipes;
        std::vector<PcfElbow> elbows;
        std::vector<PcfValve> valves;
        std::vector<PcfSupport> supports;
        double defaultDia{ 150.0 };
    };

    // --------- Helpers ----------
    static std::vector<std::string> split(const std::string& s) {
        std::istringstream iss(s);
        std::vector<std::string> out;
        std::string t;
        while (iss >> t) out.push_back(t);
        return out;
    }

    static AcGePoint3d toPt(const std::vector<std::string>& t, size_t idx) {
        return AcGePoint3d(
            std::stod(t[idx]),
            std::stod(t[idx + 1]),
            std::stod(t[idx + 2]));
    }

    static bool parsePcf(const std::wstring& file, PcfData& d) {
        std::ifstream in(file);
        if (!in) return false;
        std::string line;
        enum class Sec { None, Pipe, Elbow, Valve, Support } sec = Sec::None;
        PcfPipe curP; PcfElbow curE; PcfValve curV; PcfSupport curS;

        while (std::getline(in, line)) {
            if (line.empty()) continue;
            auto t = split(line);
            if (t.empty()) continue;
            if (t[0] == "PIPE") { sec = Sec::Pipe;   curP = PcfPipe{}; continue; }
            if (t[0] == "ELBOW") { sec = Sec::Elbow;  curE = PcfElbow{}; continue; }
            if (t[0] == "VALVE") { sec = Sec::Valve;  curV = PcfValve{}; continue; }
            if (t[0] == "SUPPORT") { sec = Sec::Support; curS = PcfSupport{}; continue; }

            if (sec == Sec::Pipe) {
                if (t[0] == "COMPONENT-IDENTIFIER") curP.id = std::stoi(t[1]);
                else if (t[0] == "END-POINT" && t.size() >= 5) {
                    if (curP.p1 == AcGePoint3d()) curP.p1 = toPt(t, 1); else curP.p2 = toPt(t, 1);
                }
                else if (t[0] == "COMPONENT-ATTRIBUTE3") curP.dia = std::stod(t[1]);
                else if (t[0] == "ITEM-DESCRIPTION") {
                    d.pipes.push_back(curP); sec = Sec::None;
                }
            }
            else if (sec == Sec::Elbow) {
                if (t[0] == "COMPONENT-IDENTIFIER") curE.id = std::stoi(t[1]);
                else if (t[0] == "END-POINT" && t.size() >= 5) {
                    if (curE.p1 == AcGePoint3d()) curE.p1 = toPt(t, 1); else curE.p2 = toPt(t, 1);
                }
                else if (t[0] == "CENTRE-POINT" && t.size() >= 4) curE.center = toPt(t, 1);
                else if (t[0] == "COMPONENT-ATTRIBUTE3") curE.dia = std::stod(t[1]);
                else if (t[0] == "ANGLE") curE.angleDeg = std::stod(t[1]) / 100.0;
                else if (t[0] == "ITEM-DESCRIPTION") { d.elbows.push_back(curE); sec = Sec::None; }
            }
            else if (sec == Sec::Valve) {
                if (t[0] == "COMPONENT-IDENTIFIER") curV.id = std::stoi(t[1]);
                else if (t[0] == "END-POINT" && t.size() >= 5) {
                    if (curV.p1 == AcGePoint3d()) curV.p1 = toPt(t, 1); else curV.p2 = toPt(t, 1);
                }
                else if (t[0] == "CENTRE-POINT" && t.size() >= 4) curV.center = toPt(t, 1);
                else if (t[0] == "COMPONENT-ATTRIBUTE3") curV.dia = std::stod(t[1]);
                else if (t[0] == "ITEM-DESCRIPTION") { d.valves.push_back(curV); sec = Sec::None; }
            }
            else if (sec == Sec::Support) {
                if (t[0] == "COMPONENT-IDENTIFIER") curS.id = std::stoi(t[1]);
                else if (t[0] == "CO-ORDS" && t.size() >= 4) curS.pt = toPt(t, 1);
                else if (t[0] == "ITEM-CODE" || t[0] == "ITEM-DESCRIPTION") {
                    d.supports.push_back(curS); sec = Sec::None;
                }
            }
        }
        // default diameter from first non-zero
        for (auto& p : d.pipes) if (p.dia > 0) { d.defaultDia = p.dia; break; }
        for (auto& e : d.elbows) if (e.dia > 0) { d.defaultDia = e.dia; break; }
        for (auto& v : d.valves) if (v.dia > 0) { d.defaultDia = v.dia; break; }
        return true;
    }

    // Найти сегмент оси, ближайший к точке, и профиль в точке
    static bool findSegByPoint(vCSDragManager* dm, const AcDbObjectId& axisId, const AcGePoint3d& pt,
        vCS_DM_Seg*& outSeg, AcGePoint3d& outClosest, const vCSProfileBase*& outProfile)
    {
        outSeg = nullptr; outProfile = nullptr;
        AcDbObjectIdArray segIds;
        vCSTools::GetSegments(axisId, segIds);
        double best = 1e300;
        for (int i = 0; i < segIds.length(); ++i) {
            vCS_DM_Seg* s = dm->GetSeg(segIds[i]);
            if (!s) continue;
            AcGePoint3d pc; s->get_closest_point(pt, pc);
            double d2 = (pc - pt).lengthSqrd();
            if (d2 < best) {
                best = d2;
                outSeg = s;
                outClosest = pc;
            }
        }
        if (!outSeg) return false;

        double dist = 0.0; outSeg->GetDistFromPrev(outClosest, dist, true);
        vCSProfilePtr profPtr; AcGeVector3d vOff;
        if (!outSeg->ArrDistProfileGetByDist(dist, profPtr, vOff) || profPtr.IsNull()) return false;
        outProfile = profPtr;
        return true;
    }

} // namespace

// -------- Импорт PCF в одну модель --------
void importPcfCmd()
{
    // 1) запрос файла
    wchar_t pathBuf[MAX_PATH] = L"";
    struct resbuf result;
    if (acedGetFileD(L"PCF файл", nullptr, L"pcf", 0, &result) != RTNORM) {
        acutPrintf(L"\nОтмена.");
        return;
    }
    if (result.restype == RTSHORT || result.resval.rstring == nullptr) {
        acutPrintf(L"\nОтмена.");
        return;
    }
    wcsncpy_s(pathBuf, result.resval.rstring, _TRUNCATE);
    acutRelRb(&result);

    PcfData pcf;
    if (!parsePcf(pathBuf, pcf)) { acutPrintf(L"\nНе удалось прочитать PCF."); return; }

    // 2) собрать путь (упрощённо: по PIPE и ELBOW в порядке файла)
    AcGePoint3dArray path;
    for (auto& p : pcf.pipes) {
        if (path.isEmpty()) path.append(p.p1);
        path.append(p.p2);
    }
    for (auto& e : pcf.elbows) {
        // добавим излом через точку центра
        path.append(e.center);
    }
    if (path.length() < 2) { acutPrintf(L"\nВ PCF нет сегментов."); return; }

    // 3) установить стартовый диаметр
    CVCSUtils::vCSCheckSettingAndNewAxisName(nullptr, nullptr,
        vCSTools::ptt_pipe, pcf.defaultDia, -1.0, nullptr, nullptr, AcDbObjectId::kNull);

    // 4) создать трубу
    AcDbObjectId axisId = AcDbObjectId::kNull;
    AcGeVector3d profileVN = AcGeVector3d::kZAxis;
    if (!vCSCreatePipeOnPoints::Create(path, axisId, &profileVN) || axisId.isNull()) {
        acutPrintf(L"\nНе удалось создать трубу.");
        return;
    }
    acutPrintf(L"\nТруба создана, axisId=%ld", axisId.asOldId());

    // 5) вставка фитингов (valve) и опор
    vCSDragManagerSmart dms; auto* dm = dms.operator->();

    // VALVE: вставляем inline (тип til_inline) в центре
    for (auto& v : pcf.valves) {
        vCS_DM_Seg* seg = nullptr; AcGePoint3d pc; const vCSProfileBase* prof = nullptr;
        if (!findSegByPoint(dm, axisId, v.center, seg, pc, prof)) {
            acutPrintf(L"\n⚠ Valve %d: не найден сегмент.", v.id);
            continue;
        }
        dm->m_InLineCreate.ClearDMTypes();
        dm->m_InLineCreate.SetPickedSegID(seg->OID());
        unsigned int nType = vCSILBase::til_inline;
        Acad::ErrorStatus es = dm->m_InLineCreate.ReCalculateModelMainInLineCreate(
            seg->OID(), prof, pc, nType, nullptr, false, nullptr);
        if (es != Acad::eOk) acutPrintf(L"\n⚠ Valve %d: ошибка %d", v.id, es);
    }

    // SUPPORT: ставим опору в точке
    for (auto& s : pcf.supports) {
        vCS_DM_Seg* seg = nullptr; AcGePoint3d pc; const vCSProfileBase* prof = nullptr;
        if (!findSegByPoint(dm, axisId, s.pt, seg, pc, prof)) {
            acutPrintf(L"\n⚠ Support %d: не найден сегмент.", s.id);
            continue;
        }
        dm->m_SupportCreate.ClearDMTypes();
        dm->m_SupportCreate.SetPickedSegID(seg->OID());
        bool checkMinidir = true, bDialogDraw = false;
        Acad::ErrorStatus es = dm->m_SupportCreate.ReCalculateModelMainSupportCreate(
            seg->OID(), prof, pc,
            checkMinidir, bDialogDraw,
            AcDbObjectId::kNull, nullptr,
            st_Support, true, false);
        if (es != Acad::eOk) acutPrintf(L"\n⚠ Support %d: ошибка %d", s.id, es);
    }

    dms.commit();
    acutPrintf(L"\n✅ Импорт PCF завершён.");
}

// ncrxEntryPoint реализована в HelloNRX.cpp