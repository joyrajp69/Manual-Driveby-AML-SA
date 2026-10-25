#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <unordered_map>
#include <string>

MYMOD(net.PS2Revival.ManualDriveby, GTA:SA Manual Driveby Refixed, 1.1, devil_desi)

// ============================================================================
// 1. ENGINE TYPES & 64-BIT STRUCTURES
// ============================================================================
struct CVector {
    float x, y, z;
};

enum eWeaponType : uint32_t {
    WEAPON_UNARMED = 0,
    WEAPON_GRENADE = 16,
    WEAPON_TEARGAS = 17,
    WEAPON_MOLOTOV = 18,
    WEAPON_PISTOL = 22,
    WEAPON_PISTOL_SILENCED = 23,
    WEAPON_DESERT_EAGLE = 24,
    WEAPON_SHOTGUN = 25,
    WEAPON_SAWNOFF = 26,
    WEAPON_SPAS12 = 27,
    WEAPON_MICRO_UZI = 28,
    WEAPON_MP5 = 29,
    WEAPON_AK47 = 30,
    WEAPON_M4 = 31,
    WEAPON_TEC9 = 32,
    WEAPON_COUNTRYRIFLE = 33,
    WEAPON_SNIPERRIFLE = 34,
    WEAPON_RLAUNCHER = 35,
    WEAPON_RLAUNCHER_HS = 36,
    WEAPON_FLAMETHROWER = 37,
    WEAPON_MINIGUN = 38,
    WEAPON_SATCHEL = 39,
    WEAPON_DETONATOR = 40,
    WEAPON_SPRAYCAN = 41,
    WEAPON_EXTINGUISHER = 42,
    WEAPON_CAMERA = 43
};

enum eWeaponFire : uint32_t {
    WEAPON_FIRE_MELEE = 0,
    WEAPON_FIRE_INSTANT_HIT = 1,
    WEAPON_FIRE_PROJECTILE = 2,
    WEAPON_FIRE_AREA_EFFECT = 3,
    WEAPON_FIRE_CAMERA = 4,
    WEAPON_FIRE_USE = 5
};

enum eVehicleClass : int32_t {
    VEHICLE_AUTOMOBILE = 0,
    VEHICLE_MTRUCK = 1,
    VEHICLE_QUAD = 2,
    VEHICLE_HELI = 3,
    VEHICLE_PLANE = 4,
    VEHICLE_BOAT = 5,
    VEHICLE_TRAIN = 6,
    VEHICLE_FHELI = 7,
    VEHICLE_FPLANE = 8,
    VEHICLE_BIKE = 9,
    VEHICLE_BMX = 10,
    VEHICLE_TRAILER = 11
};

class CPed;
class CPlayerPed;
class CVehicle;
class CEntity;
class CTask;
class CTaskSimpleGangDriveBy;
class CWeaponInfo;
class CPad;
class CAnimBlendAssociation;

// ============================================================================
// 2. CONFIGURATION STRUCTURE (PARSED FROM DrivebySettings.ini)
// ============================================================================
struct SkillRates {
    float poor = -2.0f;
    float gangster = -2.0f;
    float hitman = -2.0f;
};

struct DrivebySettings {
    // [MAIN]
    int drivebyControlType = 1;
    bool preLoadIFP = true;
    bool disableBikeWeakPoint = true;
    int noDrivingWhileAiming = 0;
    bool prepareThrowAnim = true;
    bool closeWindowsAgain = true;
    int windshieldMode = 1;
    int fixPedReactions = 2;
    bool recruitBlip = true;
    float crosshairExpMult = 0.75f;

    // [WEAPONSWITCH]
    int dpadButtonsMode = 1;
    int sideLookButtons = 1;
    bool enableOnStory = true;

    // [WEAPONSETTINGS]
    bool fixShootRateOfNPC = true;
    SkillRates defaultRates = { 1.0f, 1.0f, 1.0f };
    SkillRates weaponRates[256];

    // [CAMERA]
    float distCar[3]    = { 5.0f, 10.0f, 17.0f };
    float distBike[3]   = { 2.8f, 4.4f, 17.3f };
    float heightCar[3]  = { 0.5f, 0.55f, 0.60f };
    float heightBike[3] = { 0.2f, 0.2f, 0.2f };
    float tweakFOV[3]   = { 55.0f, 60.0f, 65.0f };
    int restoreDistance = 0;
    int jumpcutRestore = 1;
    float jumpcutCarSpeed = 8.0f;
    int keepAiming = 0;
    bool freeLineOfSight = true;
    bool noAngleLimit = true;

    // [EXCEPTIONS]
    bool disableOnMission = false;
    uint8_t classMaskCars   = 0b1110; // Bit 1: InstantHit, Bit 2: Projectile, Bit 3: AreaEffect
    uint8_t classMaskBikes  = 0b1110;
    uint8_t classMaskBoats  = 0b1110;
    uint8_t classMaskHelis  = 0b1110;
    uint8_t classMaskPlanes = 0b1110;
    uint8_t classMaskTrains = 0b1110;
    std::unordered_map<int, uint8_t> modelExceptions;

    // [CARSWITHOUTWINDOWS]
    std::unordered_map<int, int> carsWithoutWindows;
} gSettings;

// Runtime State
static bool g_bInManualDriveby = false;
static bool g_bPrevToggleCombo = false;
static bool g_bIfpPreloaded = false;
static uint32_t g_nLastSwitchTick = 0;
static CVehicle* g_pCurrentVeh = nullptr;
static CVehicle* g_pLastActiveVeh = nullptr;
static float g_fCachedPlayerShootRate = 1.0f;

// ============================================================================
// 3. ENGINE FUNCTION POINTERS (64-BIT AML 2.10 SYMBOLS)
// ============================================================================
void* hGTASA = nullptr;
uintptr_t pGTASA = 0;

CPlayerPed* (*FindPlayerPed)(int playerId) = nullptr;
CVehicle*   (*FindPlayerVehicle)(int playerId, bool bIncludeRemote) = nullptr;
CPad*       (*GetPad)(int padNumber) = nullptr;
CWeaponInfo*(*GetWeaponInfo)(eWeaponType weaponId, int8_t skill) = nullptr;
uint8_t     (*GetWeaponSkill)(CPed* pPed) = nullptr;
void        (*SetCurrentWeapon)(CPed* pPed, int slot) = nullptr;
void        (*DamageCarPanel)(CVehicle* pVeh, int panelId) = nullptr;
void        (*SetWindowOpenFlag)(CVehicle* pVeh, uint8_t doorId) = nullptr;
void        (*ClearWindowOpenFlag)(CVehicle* pVeh, uint8_t doorId) = nullptr;
void        (*ReportFrontendAudioEvent)(void* pAudioEngine, int eventId, float vol, float speed) = nullptr;
int32_t     (*GetAnimationBlockIndex)(const char* name) = nullptr;
void        (*RequestModel)(int32_t modelId, int32_t flags) = nullptr;
void        (*RestoreCameraJumpCut)(void* pCamera) = nullptr;

void*       (*OperatorNew)(size_t size) = nullptr;
void        (*TaskDriveByCtor)(CTaskSimpleGangDriveBy* self, CEntity* targetEntity, const CVector* targetPos,
                               float radius, int8_t freqSub, int8_t drivebyStyle, bool bLHS) = nullptr;
void        (*SetTaskSecondary)(void* pTaskMgr, CTask* pTask, int slot) = nullptr;
CTask*      (*GetTaskSecondary)(void* pTaskMgr, int slot) = nullptr;
CAnimBlendAssociation* (*RpAnimBlendClumpGetAssociation)(void* pClump, const char* animName) = nullptr;

// Globals
void*     pTheCamera = nullptr;
void*     pAudioEngine = nullptr;
uint32_t* pTimeInMilliseconds = nullptr;
uint32_t* pOnAMissionFlag = nullptr;

// ============================================================================
// 4. CUSTOM PARSER FOR ORIGINAL DrivebySettings.ini
// ============================================================================
static std::string TrimStr(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) start++;
    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1])) end--;
    return s.substr(start, end - start);
}

static uint8_t ParseExceptionMask(const char* val) {
    int b1 = 0, b2 = 0, b3 = 0;
    if (std::sscanf(val, "%d / %d / %d", &b1, &b2, &b3) == 3 ||
        std::sscanf(val, "%d/%d/%d", &b1, &b2, &b3) == 3) {
        uint8_t mask = 0;
        if (b1 > 0) mask |= (1 << 1);
        if (b2 > 0) mask |= (1 << 2);
        if (b3 > 0) mask |= (1 << 3);
        return mask;
    }
    return 0b1110;
}

void LoadDrivebySettingsINI() {
    // Default hardcoded windowless vehicles from original INI
    int defaultWindowless[][2] = {
        {406, 2}, {530, 2}, {485, 2}, {568, 1}, {457, 1}, {533, 1},
        {439, 1}, {429, 1}, {480, 1}, {555, 1}, {536, 1}, {575, 1},
        {567, 1}, {592, 1}, {577, 1}, {553, 1}, {519, 1}
    };
    for (auto& entry : defaultWindowless) {
        gSettings.carsWithoutWindows[entry[0]] = entry[1];
    }

    char iniPath[512];
    std::snprintf(iniPath, sizeof(iniPath), "%s/DrivebySettings.ini", aml->GetConfigPath());
    FILE* fp = std::fopen(iniPath, "r");
    if (!fp) {
        // Fallback to cleo folder path if user placed it there
        fp = std::fopen("/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/cleo/DrivebySettings.ini", "r");
    }
    if (!fp) {
        logger->Info("DrivebySettings.ini not found, using built-in defaults.");
        return;
    }

    std::string currentSection;
    char lineBuf[512];
    while (std::fgets(lineBuf, sizeof(lineBuf), fp)) {
        std::string line(lineBuf);

        // Strip comments (; or // or #)
        size_t posSemicolon = line.find(';');
        if (posSemicolon != std::string::npos) line.erase(posSemicolon);
        size_t posSlashes = line.find("//");
        if (posSlashes != std::string::npos) line.erase(posSlashes);

        line = TrimStr(line);
        if (line.empty() || line[0] == '#' || line[0] == '_') continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = TrimStr(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = TrimStr(line.substr(0, eqPos));
        std::string val = TrimStr(line.substr(eqPos + 1));
        if (key.empty() || val.empty()) continue;

        if (currentSection == "MAIN") {
            if (key == "DrivebyControlType")   gSettings.drivebyControlType = std::atoi(val.c_str());
            else if (key == "PreLoadIFP")      gSettings.preLoadIFP = (std::atoi(val.c_str()) > 0);
            else if (key == "DisableBikeWeakPoint") gSettings.disableBikeWeakPoint = (std::atoi(val.c_str()) > 0);
            else if (key == "NoDrivingWhileAiming") gSettings.noDrivingWhileAiming = std::atoi(val.c_str());
            else if (key == "PrepareThrowAnim")     gSettings.prepareThrowAnim = (std::atoi(val.c_str()) > 0);
            else if (key == "CloseWindowsAgain")    gSettings.closeWindowsAgain = (std::atoi(val.c_str()) > 0);
            else if (key == "WindshieldMode")       gSettings.windshieldMode = std::atoi(val.c_str());
            else if (key == "FixPedReactions")      gSettings.fixPedReactions = std::atoi(val.c_str());
            else if (key == "RecruitBlip")          gSettings.recruitBlip = (std::atoi(val.c_str()) > 0);
            else if (key == "CrosshairExpMult")     gSettings.crosshairExpMult = std::strtof(val.c_str(), nullptr);
        }
        else if (currentSection == "WEAPONSWITCH") {
            if (key == "DPadButtonsMode")      gSettings.dpadButtonsMode = std::atoi(val.c_str());
            else if (key == "SideLookButtons") gSettings.sideLookButtons = std::atoi(val.c_str());
            else if (key == "EnableOnStory")   gSettings.enableOnStory = (std::atoi(val.c_str()) > 0);
        }
        else if (currentSection == "WEAPONSETTINGS") {
            if (key == "FixShootRateOfNPC") {
                gSettings.fixShootRateOfNPC = (std::atoi(val.c_str()) > 0);
            } else if (key == "Default") {
                std::sscanf(val.c_str(), "%f / %f / %f",
                            &gSettings.defaultRates.poor,
                            &gSettings.defaultRates.gangster,
                            &gSettings.defaultRates.hitman);
            } else if (std::isdigit((unsigned char)key[0])) {
                int weapId = std::atoi(key.c_str());
                if (weapId >= 0 && weapId < 256) {
                    std::sscanf(val.c_str(), "%f / %f / %f",
                                &gSettings.weaponRates[weapId].poor,
                                &gSettings.weaponRates[weapId].gangster,
                                &gSettings.weaponRates[weapId].hitman);
                }
            }
        }
        else if (currentSection == "CAMERA") {
            if (key == "DistCar")         std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.distCar[0], &gSettings.distCar[1], &gSettings.distCar[2]);
            else if (key == "DistBike")   std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.distBike[0], &gSettings.distBike[1], &gSettings.distBike[2]);
            else if (key == "HeightCar")  std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.heightCar[0], &gSettings.heightCar[1], &gSettings.heightCar[2]);
            else if (key == "HeightBike") std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.heightBike[0], &gSettings.heightBike[1], &gSettings.heightBike[2]);
            else if (key == "TweakFOV")   std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.tweakFOV[0], &gSettings.tweakFOV[1], &gSettings.tweakFOV[2]);
            else if (key == "RestoreDistance") gSettings.restoreDistance = std::atoi(val.c_str());
            else if (key == "JumpcutRestore")  gSettings.jumpcutRestore = std::atoi(val.c_str());
            else if (key == "JumpcutCarSpeed") gSettings.jumpcutCarSpeed = std::strtof(val.c_str(), nullptr);
            else if (key == "KeepAiming")      gSettings.keepAiming = std::atoi(val.c_str());
            else if (key == "FreeLineOfSight") gSettings.freeLineOfSight = (std::atoi(val.c_str()) > 0);
            else if (key == "NoAngleLimit")    gSettings.noAngleLimit = (std::atoi(val.c_str()) > 0);
        }
        else if (currentSection == "EXCEPTIONS") {
            if (key == "DisableOnMission") gSettings.disableOnMission = (std::atoi(val.c_str()) > 0);
            else if (key == "Cars")   gSettings.classMaskCars   = ParseExceptionMask(val.c_str());
            else if (key == "Bikes")  gSettings.classMaskBikes  = ParseExceptionMask(val.c_str());
            else if (key == "Boats")  gSettings.classMaskBoats  = ParseExceptionMask(val.c_str());
            else if (key == "Helis")  gSettings.classMaskHelis  = ParseExceptionMask(val.c_str());
            else if (key == "Planes") gSettings.classMaskPlanes = ParseExceptionMask(val.c_str());
            else if (key == "Trains") gSettings.classMaskTrains = ParseExceptionMask(val.c_str());
            else if (std::isdigit((unsigned char)key[0])) {
                int modelId = std::atoi(key.c_str());
                gSettings.modelExceptions[modelId] = ParseExceptionMask(val.c_str());
            }
        }
        else if (currentSection == "CARSWITHOUTWINDOWS") {
            if (std::isdigit((unsigned char)key[0])) {
                int modelId = std::atoi(key.c_str());
                gSettings.carsWithoutWindows[modelId] = std::atoi(val.c_str());
            }
        }
    }
    std::fclose(fp);
    logger->Info("DrivebySettings.ini loaded successfully!");
}

// ============================================================================
// 5. ENGINE HELPER FUNCTIONS (ARM64 SAFE)
// ============================================================================
inline uint32_t GetTicks() {
    return pTimeInMilliseconds ? *pTimeInMilliseconds : 0;
}

inline int16_t GetEntityModelIndex(CEntity* pEntity) {
    if (!pEntity) return -1;
    // In GTA SA 64-bit v2.10, CEntity::m_nModelIndex is at offset 0x26
    return *(int16_t*)((uintptr_t)pEntity + 0x26);
}

inline eVehicleClass GetVehicleClass(CVehicle* pVeh) {
    if (!pVeh) return VEHICLE_AUTOMOBILE;
    // In GTA SA 64-bit v2.10, CVehicle::m_dwVehicleSubClass is at offset 0x5A4
    return *(eVehicleClass*)((uintptr_t)pVeh + 0x5A4);
}

uint8_t GetAllowedWeaponMaskForVehicle(CVehicle* pVeh) {
    if (!pVeh) return 0;
    int modelId = GetEntityModelIndex((CEntity*)pVeh);
    auto it = gSettings.modelExceptions.find(modelId);
    if (it != gSettings.modelExceptions.end()) {
        return it->second;
    }

    eVehicleClass vClass = GetVehicleClass(pVeh);
    switch (vClass) {
        case VEHICLE_BIKE:
        case VEHICLE_BMX:
        case VEHICLE_QUAD:
            return gSettings.classMaskBikes;
        case VEHICLE_HELI:
        case VEHICLE_FHELI:
            return gSettings.classMaskHelis;
        case VEHICLE_PLANE:
        case VEHICLE_FPLANE:
            return gSettings.classMaskPlanes;
        case VEHICLE_BOAT:
            return gSettings.classMaskBoats;
        case VEHICLE_TRAIN:
            return gSettings.classMaskTrains;
        default:
            return gSettings.classMaskCars;
    }
}

// Ported exact CLEO shoot rate calculation (:GetShootRate)
float ComputePedDrivebyShootRate(CPed* pPed, CVehicle* pVeh) {
    if (!pPed || !GetWeaponInfo) return 1.0f;

    uint8_t activeSlot = *(uint8_t*)((uintptr_t)pPed + 0x8E8);
    if (activeSlot > 12) return 1.0f;

    uintptr_t weaponBase = (uintptr_t)pPed + 0x710 + (activeSlot * 0x20);
    eWeaponType weapId = *(eWeaponType*)(weaponBase + 0x00);
    if (weapId == WEAPON_UNARMED || weapId >= 256) return 1.0f;

    int8_t skill = GetWeaponSkill ? GetWeaponSkill(pPed) : 1;
    if (skill < 0) skill = 0;
    if (skill > 2) skill = 2;

    CWeaponInfo* pInfo = GetWeaponInfo(weapId, skill);
    if (!pInfo) pInfo = GetWeaponInfo(weapId, 1);
    if (!pInfo) return 1.0f;

    // Select configured multiplier for Poor (0), Gangster (1), Hitman (2)
    const SkillRates& sr = gSettings.weaponRates[weapId];
    float rateMult = (skill == 0) ? sr.poor : ((skill == 1) ? sr.gangster : sr.hitman);
    if (rateMult == -2.0f) {
        rateMult = (skill == 0) ? gSettings.defaultRates.poor :
                   ((skill == 1) ? gSettings.defaultRates.gangster : gSettings.defaultRates.hitman);
    }

    if (rateMult <= 0.0f) return 0.0f;
    if (rateMult > 9.99f) rateMult = 1.0f;

    // Read AnimLoop2Start (0x4C) & AnimLoop2End (0x50) from CWeaponInfo
    float animLoopStart = *(float*)((uintptr_t)pInfo + 0x4C);
    float animLoopEnd   = *(float*)((uintptr_t)pInfo + 0x50);
    float loopDelta     = animLoopEnd - animLoopStart;

    if (loopDelta > 0.001f) {
        float animFactor = std::pow(loopDelta, -0.9596f) * 0.1749f;
        uint32_t flags = *(uint32_t*)((uintptr_t)pInfo + 0x18);
        if (flags == 0x3833) { // Dual wielded compensation
            animFactor *= 1.06f;
        }
        rateMult *= animFactor;
    }

    if (rateMult < 0.01f) rateMult = 0.01f;
    if (rateMult > 8.0f)  rateMult = 8.0f;
    return rateMult;
}

bool IsWeaponSelectableForDriveby(CPed* pPed, CVehicle* pVeh, eWeaponType weapId) {
    if (weapId == WEAPON_UNARMED) return true;
    if (!GetWeaponInfo) return true;

    CWeaponInfo* pInfo = GetWeaponInfo(weapId, 1);
    if (!pInfo) return false;

    uint32_t fireType = *(uint32_t*)((uintptr_t)pInfo + 0x00);
    if (fireType == WEAPON_FIRE_CAMERA) fireType = WEAPON_FIRE_INSTANT_HIT;
    if (fireType < 1 || fireType > 3) return false;

    // Submachine guns (Slot 5: Micro Uzi, MP5, Tec-9) are always allowed in vanilla
    if (weapId != WEAPON_MICRO_UZI && weapId != WEAPON_MP5 && weapId != WEAPON_TEC9) {
        uint8_t allowedMask = GetAllowedWeaponMaskForVehicle(pVeh);
        if (!(allowedMask & (1 << fireType))) return false;
    }

    // Check if disabled (-1.0) in [WEAPONSETTINGS]
    const SkillRates& sr = gSettings.weaponRates[weapId];
    float cfgVal = sr.gangster;
    if (cfgVal == -2.0f) cfgVal = gSettings.defaultRates.gangster;
    return (cfgVal >= 0.0f);
}

void CycleDrivebyWeapon(CPlayerPed* pPlayer, CVehicle* pVeh, int dir) {
    if (!pPlayer || !SetCurrentWeapon) return;
    uint32_t now = GetTicks();
    if (now - g_nLastSwitchTick < 200) return;
    g_nLastSwitchTick = now;

    int curSlot = *(uint8_t*)((uintptr_t)pPlayer + 0x8E8);
    for (int i = 0; i < 13; ++i) {
        curSlot = (curSlot + dir + 13) % 13;
        uintptr_t weaponBase = (uintptr_t)pPlayer + 0x710 + (curSlot * 0x20);
        eWeaponType weapId = *(eWeaponType*)(weaponBase + 0x00);
        int32_t totalAmmo  = *(int32_t*)(weaponBase + 0x0C);

        if (curSlot == 0 || (totalAmmo > 0 && IsWeaponSelectableForDriveby((CPed*)pPlayer, pVeh, weapId))) {
            SetCurrentWeapon((CPed*)pPlayer, curSlot);
            g_fCachedPlayerShootRate = ComputePedDrivebyShootRate((CPed*)pPlayer, pVeh);
            if (ReportFrontendAudioEvent && pAudioEngine) {
                ReportFrontendAudioEvent(pAudioEngine, 32, 1.0f, 1.0f);
            }
            break;
        }
    }
}

void ApplyDrivebyShootRate(CPed* pPed, float rateMult) {
    if (!pPed || !RpAnimBlendClumpGetAssociation || rateMult <= 0.0f) return;
    void* pRpClump = *(void**)((uintptr_t)pPed + 0x18); // CEntity::m_pRwObject
    if (!pRpClump) return;

    static const char* drivebyAnims[] = {
        "gang_drivebylhs", "gang_drivebyrhs",
        "gang_drivebylhs_fwd", "gang_drivebylhs_bwd",
        "gang_drivebytop_lhs", "gang_drivebytop_rhs",
        "gang_drivebyrhs_fwd", "gang_drivebyrhs_bwd",
        "pass_driveby_lhs", "pass_driveby_rhs",
        "pass_driveby_fwd", "pass_driveby_bwd",
        "cop_dvby_l", "cop_dvby_r", "cop_dvby_ft", "cop_dvby_b"
    };

    for (const char* animName : drivebyAnims) {
        CAnimBlendAssociation* pAssoc = RpAnimBlendClumpGetAssociation(pRpClump, animName);
        if (pAssoc) {
            *(float*)((uintptr_t)pAssoc + 0x2C) = rateMult;
            break;
        }
    }
}

void ManageVehicleWindows(CVehicle* pVeh, bool bOpen) {
    if (!pVeh) return;
    int modelId = GetEntityModelIndex((CEntity*)pVeh);
    auto it = gSettings.carsWithoutWindows.find(modelId);
    if (it != gSettings.carsWithoutWindows.end() && it->second >= 1) {
        return; // Vehicle has no door windows, prevent crash
    }

    eVehicleClass vClass = GetVehicleClass(pVeh);
    if (vClass != VEHICLE_AUTOMOBILE && vClass != VEHICLE_MTRUCK) return;

    if (bOpen && SetWindowOpenFlag) {
        SetWindowOpenFlag(pVeh, 8);  // Right front
        SetWindowOpenFlag(pVeh, 10); // Left front
    } else if (!bOpen && gSettings.closeWindowsAgain && ClearWindowOpenFlag) {
        ClearWindowOpenFlag(pVeh, 8);
        ClearWindowOpenFlag(pVeh, 9);
        ClearWindowOpenFlag(pVeh, 10);
        ClearWindowOpenFlag(pVeh, 11);
    }
}

// ============================================================================
// 6. ENGINE HOOKS (ARM64)
// ============================================================================

// Suppress side/behind look when in manual driveby or using SideLookButtons for weapon switching
DECL_HOOK(bool, CPad_GetLookLeft, CPad* self) {
    if (g_bInManualDriveby || (g_pCurrentVeh && gSettings.sideLookButtons == 1)) return false;
    return CPad_GetLookLeft(self);
}

DECL_HOOK(bool, CPad_GetLookRight, CPad* self) {
    if (g_bInManualDriveby || (g_pCurrentVeh && gSettings.sideLookButtons == 1)) return false;
    return CPad_GetLookRight(self);
}

DECL_HOOK(bool, CPad_GetLookBehindForCar, CPad* self) {
    if (g_bInManualDriveby) return false;
    return CPad_GetLookBehindForCar(self);
}

// Suppress steering/driving while aiming if NoDrivingWhileAiming > 0
DECL_HOOK(int16_t, CPad_GetSteeringLeftRight, CPad* self) {
    if (g_bInManualDriveby && gSettings.noDrivingWhileAiming > 0 && g_pCurrentVeh) {
        eVehicleClass vc = GetVehicleClass(g_pCurrentVeh);
        bool isBike = (vc == VEHICLE_BIKE || vc == VEHICLE_BMX);
        if (gSettings.noDrivingWhileAiming == 1 || (gSettings.noDrivingWhileAiming == 2 && isBike)) {
            return 0;
        }
    }
    return CPad_GetSteeringLeftRight(self);
}

// Driveby Task Processing: Shoot Rate, Windshield Breaking, Crosshair Spread
DECL_HOOK(bool, CTaskSimpleGangDriveBy_ProcessPed, CTaskSimpleGangDriveBy* self, CPed* pPed) {
    bool res = CTaskSimpleGangDriveBy_ProcessPed(self, pPed);

    CPlayerPed* pPlayer = FindPlayerPed ? FindPlayerPed(-1) : nullptr;
    if (pPed == (CPed*)pPlayer && g_bInManualDriveby) {
        ApplyDrivebyShootRate(pPed, g_fCachedPlayerShootRate);

        // Break windshield when aiming/firing forward (WindshieldMode > 0)
        if (gSettings.windshieldMode > 0 && g_pCurrentVeh && DamageCarPanel) {
            int modelId = GetEntityModelIndex((CEntity*)g_pCurrentVeh);
            auto it = gSettings.carsWithoutWindows.find(modelId);
            bool hasWindshield = (it == gSettings.carsWithoutWindows.end() || it->second < 2);
            if (hasWindshield) {
                int8_t animDir = *(int8_t*)((uintptr_t)self + 0x24);
                if (animDir == 0) { // Forward direction
                    DamageCarPanel(g_pCurrentVeh, 4);
                }
            }
        }
    } else if (gSettings.fixShootRateOfNPC && pPed != (CPed*)pPlayer) {
        float npcRate = ComputePedDrivebyShootRate(pPed, nullptr);
        ApplyDrivebyShootRate(pPed, npcRate);
    }

    return res;
}

// Main Game Loop Hook
DECL_HOOKv(CGame_Process) {
    CGame_Process();

    if (!FindPlayerPed || !FindPlayerVehicle) return;

    // Preload drivebys.ifp once (`PreLoadIFP = 1`)
    if (gSettings.preLoadIFP && !g_bIfpPreloaded && GetAnimationBlockIndex && RequestModel) {
        int32_t ifpIdx = GetAnimationBlockIndex("drivebys");
        if (ifpIdx != -1) {
            RequestModel(ifpIdx + 25557, 0x02); // Keep IFP loaded
            g_bIfpPreloaded = true;
        }
    }

    if (gSettings.disableOnMission && pOnAMissionFlag && *pOnAMissionFlag != 0) {
        g_bInManualDriveby = false;
        return;
    }

    CPlayerPed* pPlayer = FindPlayerPed(-1);
    CVehicle* pVeh = FindPlayerVehicle(-1, false);

    // Handle Leaving Vehicle (Roll up windows if CloseWindowsAgain = 1)
    if (!pPlayer || !pVeh) {
        if (g_pLastActiveVeh && g_bInManualDriveby) {
            ManageVehicleWindows(g_pLastActiveVeh, false);
        }
        g_bInManualDriveby = false;
        g_pCurrentVeh = nullptr;
        g_pLastActiveVeh = nullptr;
        return;
    }

    g_pCurrentVeh = pVeh;
    g_pLastActiveVeh = pVeh;

    CPad* pPad = GetPad ? GetPad(0) : nullptr;
    if (!pPad) return;

    // Read Raw Look-Left / Look-Right before hook suppression
    bool bLookL = CPad_GetLookLeft(pPad);
    bool bLookR = CPad_GetLookRight(pPad);
    bool bComboPressed = (bLookL && bLookR);

    // Toggle Manual Driveby when pressing LookLeft + LookRight simultaneously
    if (bComboPressed && !g_bPrevToggleCombo) {
        g_bInManualDriveby = !g_bInManualDriveby;

        void* pPedIntelligence = *(void**)((uintptr_t)pPlayer + 0x570);
        if (pPedIntelligence) {
            void* pTaskMgr = (void*)((uintptr_t)pPedIntelligence + 0x08);

            if (g_bInManualDriveby) {
                uint8_t curSlot = *(uint8_t*)((uintptr_t)pPlayer + 0x8E8);
                uintptr_t weaponBase = (uintptr_t)pPlayer + 0x710 + (curSlot * 0x20);
                eWeaponType activeWeap = *(eWeaponType*)(weaponBase + 0x00);

                if (activeWeap == WEAPON_UNARMED || !IsWeaponSelectableForDriveby((CPed*)pPlayer, pVeh, activeWeap)) {
                    CycleDrivebyWeapon(pPlayer, pVeh, 1);
                } else {
                    g_fCachedPlayerShootRate = ComputePedDrivebyShootRate((CPed*)pPlayer, pVeh);
                }

                // Roll down car windows safely
                ManageVehicleWindows(pVeh, true);

                // Start 360-degree Free-Aim Driveby Task (Style 4)
                if (OperatorNew && TaskDriveByCtor && SetTaskSecondary) {
                    CTaskSimpleGangDriveBy* pTask = (CTaskSimpleGangDriveBy*)OperatorNew(128);
                    if (pTask) {
                        CVector zeroVec = {0.0f, 0.0f, 0.0f};
                        TaskDriveByCtor(pTask, nullptr, &zeroVec, 600.0f, 100, 4, false);
                        SetTaskSecondary(pTaskMgr, (CTask*)pTask, 1);
                    }
                }
            } else {
                // Exit Manual Driveby
                if (SetTaskSecondary) {
                    SetTaskSecondary(pTaskMgr, nullptr, 1);
                }
                if (gSettings.jumpcutRestore > 0 && RestoreCameraJumpCut && pTheCamera) {
                    RestoreCameraJumpCut(pTheCamera);
                }
            }
        }
    }
    g_bPrevToggleCombo = bComboPressed;

    // Weapon Switching while in Vehicle
    bool canSwitchWeap = g_bInManualDriveby || (gSettings.sideLookButtons == 1);
    if (canSwitchWeap && !bComboPressed) {
        if (bLookR && !bLookL) {
            CycleDrivebyWeapon(pPlayer, pVeh, 1);
        } else if (bLookL && !bLookR) {
            CycleDrivebyWeapon(pPlayer, pVeh, -1);
        }
    }
}

// ============================================================================
// 7. PLUGIN INITIALIZATION
// ============================================================================
extern "C" void OnModLoad() {
    logger->SetTag("ManualDrivebySA");

    LoadDrivebySettingsINI();

    hGTASA = aml->GetLibHandle("libGTASA.so");
    pGTASA = aml->GetLib("libGTASA.so");
    if (!hGTASA || !pGTASA) {
        logger->Error("Failed to locate libGTASA.so!");
        return;
    }

    // Resolve Engine Functions via Symbols
    SET_TO(FindPlayerPed,            aml->GetSym(hGTASA, "_Z13FindPlayerPedi"));
    SET_TO(FindPlayerVehicle,        aml->GetSym(hGTASA, "_Z17FindPlayerVehicleib"));
    SET_TO(GetPad,                   aml->GetSym(hGTASA, "_ZN4CPad6GetPadEi"));
    SET_TO(GetWeaponInfo,            aml->GetSym(hGTASA, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea"));
    SET_TO(GetWeaponSkill,           aml->GetSym(hGTASA, "_ZN4CPed14GetWeaponSkillEv"));
    SET_TO(SetCurrentWeapon,         aml->GetSym(hGTASA, "_ZN4CPed16SetCurrentWeaponEi"));
    SET_TO(DamageCarPanel,           aml->GetSym(hGTASA, "_ZN11CAutomobile13SetPanelStateEii"));
    SET_TO(SetWindowOpenFlag,        aml->GetSym(hGTASA, "_ZN8CVehicle17SetWindowOpenFlagEh"));
    SET_TO(ClearWindowOpenFlag,      aml->GetSym(hGTASA, "_ZN8CVehicle19ClearWindowOpenFlagEh"));
    SET_TO(ReportFrontendAudioEvent, aml->GetSym(hGTASA, "_ZN12CAudioEngine24ReportFrontendAudioEventEiff"));
    SET_TO(GetAnimationBlockIndex,   aml->GetSym(hGTASA, "_ZN12CAnimManager22GetAnimationBlockIndexEPKc"));
    SET_TO(RequestModel,             aml->GetSym(hGTASA, "_ZN10CStreaming12RequestModelEii"));
    SET_TO(RestoreCameraJumpCut,     aml->GetSym(hGTASA, "_ZN7CCamera14RestoreWithJumpCutEv"));
    SET_TO(OperatorNew,              aml->GetSym(hGTASA, "_Znwm"));
    SET_TO(TaskDriveByCtor,          aml->GetSym(hGTASA, "_ZN22CTaskSimpleGangDriveByC2EP7CEntityPK7CVectorfaab"));
    SET_TO(SetTaskSecondary,         aml->GetSym(hGTASA, "_ZN12CTaskManager16SetTaskSecondaryEP5CTaski"));
    SET_TO(GetTaskSecondary,         aml->GetSym(hGTASA, "_ZN12CTaskManager16GetTaskSecondaryEi"));
    SET_TO(RpAnimBlendClumpGetAssociation, aml->GetSym(hGTASA, "_Z30RpAnimBlendClumpGetAssociationP7RpClumpPKc"));

    // Resolve Globals
    SET_TO(pTheCamera,               aml->GetSym(hGTASA, "TheCamera"));
    SET_TO(pAudioEngine,             aml->GetSym(hGTASA, "AEAudioHardware"));
    SET_TO(pTimeInMilliseconds,      aml->GetSym(hGTASA, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(pOnAMissionFlag,          aml->GetSym(hGTASA, "OnAMissionFlag"));

    // Install ARM64 Hooks
    HOOKSYM(CPad_GetLookLeft,                  hGTASA, "_ZN4CPad11GetLookLeftEv");
    HOOKSYM(CPad_GetLookRight,                 hGTASA, "_ZN4CPad12GetLookRightEv");
    HOOKSYM(CPad_GetLookBehindForCar,          hGTASA, "_ZN4CPad19GetLookBehindForCarEv");
    HOOKSYM(CPad_GetSteeringLeftRight,         hGTASA, "_ZN4CPad20GetSteeringLeftRightEv");
    HOOKSYM(CTaskSimpleGangDriveBy_ProcessPed, hGTASA, "_ZN22CTaskSimpleGangDriveBy10ProcessPedEP4CPed");
    HOOKSYM(CGame_Process,                     hGTASA, "_ZN5CGame7ProcessEv");

    logger->Info("Manual Driveby Refixed v1.1 (64-bit AML 2.10) initialized!");
}
