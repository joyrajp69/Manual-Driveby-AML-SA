#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

MYMODCFG(net.PS2Revival.ManualDriveby, GTA:SA Manual Driveby Refixed, 1.3, devil_desi)

// ============================================================================
// 1. ENGINE TYPES & 64-BIT STRUCTURES
// ============================================================================
struct CVector {
    float x, y, z;
};

struct CRect {
    float left, bottom, right, top;
};

struct CRGBA {
    uint8_t r, g, b, a;
    CRGBA(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a) : r(_r), g(_g), b(_b), a(_a) {}
};

struct CSprite2d {
    void* m_pTexture = nullptr;
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
// 2. CONFIGURATION STRUCTURE
// ============================================================================
struct SkillRates {
    float poor = 1.0f;
    float gangster = 1.0f;
    float hitman = 1.0f;
};

struct DrivebySettings {
    // [MAIN]
    bool enableMod = true;
    bool preLoadIFP = true;
    bool disableBikeWeakPoint = true;
    int noDrivingWhileAiming = 0;
    bool closeWindowsAgain = true;
    int windshieldMode = 1;
    float crosshairExpMult = 0.75f;

    // [TOUCH_ICON] - Cheeseburger's Driveby Icon Settings
    bool showTouchIcon = true;
    char iconTextureName[64] = "hud_driveby";
    float iconPosXRatio = 0.82f;   // 82% across screen width (Right side, near Horn/Sub-mission)
    float iconPosYRatio = 0.45f;   // 45% down screen height
    float iconSizePx = 135.0f;     // Button diameter in pixels on 1080p screen

    // [WEAPONSWITCH]
    bool allowTouchWeaponSwipe = true;
    int sideLookButtons = 1;

    // [WEAPONSETTINGS]
    bool fixShootRateOfNPC = true;
    SkillRates defaultRates = { 1.0f, 1.0f, 1.0f };
    SkillRates weaponRates[60];

    // [CAMERA]
    int jumpcutRestore = 1;
    bool freeLineOfSight = true;

    // [EXCEPTIONS]
    bool disableOnMission = false;
    uint8_t classMaskCars   = 0b1110;
    uint8_t classMaskBikes  = 0b1110;
    uint8_t classMaskBoats  = 0b1110;
    uint8_t classMaskHelis  = 0b1110;
    uint8_t classMaskPlanes = 0b1110;
    uint8_t classMaskTrains = 0b1110;

    std::unordered_map<int, int> carsWithoutWindows;
} gSettings;

// Runtime State
static bool g_bInManualDriveby = false;
static bool g_bPrevComboPressed = false;
static bool g_bTouchToggleRequested = false;
static bool g_bIfpPreloaded = false;
static bool g_bSpriteLoaded = false;
static CSprite2d g_DrivebyIconSprite;
static uint32_t g_nLastSwitchTick = 0;
static CVehicle* g_pCurrentVeh = nullptr;
static CVehicle* g_pLastActiveVeh = nullptr;
static float g_fCachedPlayerShootRate = 1.0f;

// ============================================================================
// 3. ENGINE FUNCTION POINTERS
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
bool        (*PadCycleWeaponRight)(CPad* pPad) = nullptr;
bool        (*PadCycleWeaponLeft)(CPad* pPad) = nullptr;

// Texture & Sprite2d Functions (For rendering Cheeseburger's icon from mobile.txt)
int32_t     (*TxdFindSlot)(const char* name) = nullptr;
void        (*TxdPushCurrent)(void) = nullptr;
void        (*TxdPopCurrent)(void) = nullptr;
void        (*TxdSetCurrent)(int32_t slot) = nullptr;
void        (*SpriteSetTexture)(CSprite2d* self, const char* name) = nullptr;
void        (*SpriteDraw)(CSprite2d* self, const CRect& rect, const CRGBA& color) = nullptr;

void*       (*OperatorNew)(size_t size) = nullptr;
void        (*TaskDriveByCtor)(CTaskSimpleGangDriveBy* self, CEntity* targetEntity, const CVector* targetPos,
                               float radius, int8_t freqSub, int8_t drivebyStyle, bool bLHS) = nullptr;
void        (*SetTaskSecondary)(void* pTaskMgr, CTask* pTask, int slot) = nullptr;
CTask*      (*GetTaskSecondary)(void* pTaskMgr, int slot) = nullptr;
CAnimBlendAssociation* (*RpAnimBlendClumpGetAssociation)(void* pClump, const char* animName) = nullptr;

void*     pTheCamera = nullptr;
void*     pAudioEngine = nullptr;
uint32_t* pTimeInMilliseconds = nullptr;
uint32_t* pOnAMissionFlag = nullptr;
int32_t*  pRsGlobalWidth = nullptr;
int32_t*  pRsGlobalHeight = nullptr;

// ============================================================================
// 4. AUTO-GENERATING INI LOADER
// ============================================================================
static uint8_t ParseMask(const char* str) {
    int b1 = 1, b2 = 1, b3 = 1;
    if (std::sscanf(str, "%d/%d/%d", &b1, &b2, &b3) == 3 ||
        std::sscanf(str, "%d / %d / %d", &b1, &b2, &b3) == 3) {
        uint8_t m = 0;
        if (b1 > 0) m |= (1 << 1);
        if (b2 > 0) m |= (1 << 2);
        if (b3 > 0) m |= (1 << 3);
        return m;
    }
    return 0b1110;
}

static void ParseSkillTriplet(const char* str, SkillRates& out) {
    std::sscanf(str, "%f / %f / %f", &out.poor, &out.gangster, &out.hitman);
}

void InitAndSaveConfig() {
    // [MAIN]
    gSettings.enableMod            = cfg->GetBool("EnableMod", true, "MAIN");
    gSettings.preLoadIFP           = cfg->GetBool("PreLoadIFP", true, "MAIN");
    gSettings.disableBikeWeakPoint = cfg->GetBool("DisableBikeWeakPoint", true, "MAIN");
    gSettings.noDrivingWhileAiming = cfg->GetInt("NoDrivingWhileAiming", 0, "MAIN");
    gSettings.closeWindowsAgain    = cfg->GetBool("CloseWindowsAgain", true, "MAIN");
    gSettings.windshieldMode       = cfg->GetInt("WindshieldMode", 1, "MAIN");
    gSettings.crosshairExpMult     = cfg->GetFloat("CrosshairExpMult", 0.75f, "MAIN");

    // [TOUCH_ICON]
    gSettings.showTouchIcon  = cfg->GetBool("ShowTouchIcon", true, "TOUCH_ICON");
    const char* texName      = cfg->GetString("IconTextureName", "hud_driveby", "TOUCH_ICON");
    std::snprintf(gSettings.iconTextureName, sizeof(gSettings.iconTextureName), "%s", texName);
    gSettings.iconPosXRatio  = cfg->GetFloat("IconPosXRatio", 0.82f, "TOUCH_ICON");
    gSettings.iconPosYRatio  = cfg->GetFloat("IconPosYRatio", 0.45f, "TOUCH_ICON");
    gSettings.iconSizePx     = cfg->GetFloat("IconSizePx", 135.0f, "TOUCH_ICON");

    // [WEAPONSWITCH]
    gSettings.allowTouchWeaponSwipe = cfg->GetBool("AllowTouchWeaponSwipe", true, "WEAPONSWITCH");
    gSettings.sideLookButtons       = cfg->GetInt("SideLookButtons", 1, "WEAPONSWITCH");

    // [WEAPONSETTINGS]
    gSettings.fixShootRateOfNPC = cfg->GetBool("FixShootRateOfNPC", true, "WEAPONSETTINGS");
    ParseSkillTriplet(cfg->GetString("Default", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.defaultRates);

    for (int i = 0; i < 60; ++i) gSettings.weaponRates[i] = gSettings.defaultRates;
    ParseSkillTriplet(cfg->GetString("22", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[22]);
    ParseSkillTriplet(cfg->GetString("23", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[23]);
    ParseSkillTriplet(cfg->GetString("24", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[24]);
    ParseSkillTriplet(cfg->GetString("25", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[25]);
    ParseSkillTriplet(cfg->GetString("26", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[26]);
    ParseSkillTriplet(cfg->GetString("27", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[27]);
    ParseSkillTriplet(cfg->GetString("28", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[28]);
    ParseSkillTriplet(cfg->GetString("29", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[29]);
    ParseSkillTriplet(cfg->GetString("30", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[30]);
    ParseSkillTriplet(cfg->GetString("31", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[31]);
    ParseSkillTriplet(cfg->GetString("32", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[32]);
    ParseSkillTriplet(cfg->GetString("33", "1.00 / 1.00 / 1.00", "WEAPONSETTINGS"), gSettings.weaponRates[33]);

    // [CAMERA]
    gSettings.jumpcutRestore  = cfg->GetInt("JumpcutRestore", 1, "CAMERA");
    gSettings.freeLineOfSight = cfg->GetBool("FreeLineOfSight", true, "CAMERA");

    // [EXCEPTIONS]
    gSettings.disableOnMission = cfg->GetBool("DisableOnMission", false, "EXCEPTIONS");
    gSettings.classMaskCars    = ParseMask(cfg->GetString("Cars",   "1/1/1", "EXCEPTIONS"));
    gSettings.classMaskBikes   = ParseMask(cfg->GetString("Bikes",  "1/1/1", "EXCEPTIONS"));
    gSettings.classMaskBoats   = ParseMask(cfg->GetString("Boats",  "1/1/1", "EXCEPTIONS"));
    gSettings.classMaskHelis   = ParseMask(cfg->GetString("Helis",  "1/1/1", "EXCEPTIONS"));
    gSettings.classMaskPlanes  = ParseMask(cfg->GetString("Planes", "1/1/1", "EXCEPTIONS"));
    gSettings.classMaskTrains  = ParseMask(cfg->GetString("Trains", "1/1/1", "EXCEPTIONS"));

    cfg->Save();

    int defaultWindowless[][2] = {
        {406, 2}, {530, 2}, {485, 2}, {568, 1}, {457, 1}, {533, 1},
        {439, 1}, {429, 1}, {480, 1}, {555, 1}, {536, 1}, {575, 1},
        {567, 1}, {592, 1}, {577, 1}, {553, 1}, {519, 1}
    };
    for (auto& entry : defaultWindowless) {
        gSettings.carsWithoutWindows[entry[0]] = entry[1];
    }
}

// ============================================================================
// 5. HELPER FUNCTIONS
// ============================================================================
inline uint32_t GetTicks() {
    return pTimeInMilliseconds ? *pTimeInMilliseconds : 0;
}

inline int16_t GetEntityModelIndex(CEntity* pEntity) {
    if (!pEntity) return -1;
    return *(int16_t*)((uintptr_t)pEntity + 0x26);
}

inline eVehicleClass GetVehicleClass(CVehicle* pVeh) {
    if (!pVeh) return VEHICLE_AUTOMOBILE;
    return *(eVehicleClass*)((uintptr_t)pVeh + 0x5A4);
}

inline void GetScreenResolution(float& outW, float& outH) {
    outW = (pRsGlobalWidth  && *pRsGlobalWidth  > 0) ? (float)(*pRsGlobalWidth)  : 2340.0f;
    outH = (pRsGlobalHeight && *pRsGlobalHeight > 0) ? (float)(*pRsGlobalHeight) : 1080.0f;
}

CRect GetTouchIconRect() {
    float scrW, scrH;
    GetScreenResolution(scrW, scrH);
    float scale = scrH / 1080.0f;
    float halfSize = (gSettings.iconSizePx * scale) * 0.5f;
    float centerX = scrW * gSettings.iconPosXRatio;
    float centerY = scrH * gSettings.iconPosYRatio;

    CRect r;
    r.left   = centerX - halfSize;
    r.right  = centerX + halfSize;
    r.top    = centerY - halfSize;
    r.bottom = centerY + halfSize;
    return r;
}

float ComputePedDrivebyShootRate(CPed* pPed) {
    if (!pPed || !GetWeaponInfo) return 1.0f;
    uint8_t activeSlot = *(uint8_t*)((uintptr_t)pPed + 0x8E8);
    if (activeSlot > 12) return 1.0f;

    uintptr_t weaponBase = (uintptr_t)pPed + 0x710 + (activeSlot * 0x20);
    eWeaponType weapId = *(eWeaponType*)(weaponBase + 0x00);
    if (weapId == WEAPON_UNARMED || weapId >= 60) return 1.0f;

    int8_t skill = GetWeaponSkill ? GetWeaponSkill(pPed) : 1;
    if (skill < 0) skill = 0;
    if (skill > 2) skill = 2;

    CWeaponInfo* pInfo = GetWeaponInfo(weapId, skill);
    if (!pInfo) pInfo = GetWeaponInfo(weapId, 1);
    if (!pInfo) return 1.0f;

    const SkillRates& sr = gSettings.weaponRates[weapId];
    float rateMult = (skill == 0) ? sr.poor : ((skill == 1) ? sr.gangster : sr.hitman);
    if (rateMult <= 0.0f) return 0.0f;

    float animLoopStart = *(float*)((uintptr_t)pInfo + 0x4C);
    float animLoopEnd   = *(float*)((uintptr_t)pInfo + 0x50);
    float loopDelta     = animLoopEnd - animLoopStart;

    if (loopDelta > 0.001f) {
        float animFactor = std::pow(loopDelta, -0.9596f) * 0.1749f;
        uint32_t flags = *(uint32_t*)((uintptr_t)pInfo + 0x18);
        if (flags == 0x3833) animFactor *= 1.06f;
        rateMult *= animFactor;
    }

    if (rateMult < 0.01f) rateMult = 0.01f;
    if (rateMult > 8.0f)  rateMult = 8.0f;
    return rateMult;
}

bool IsWeaponValidForDriveby(eWeaponType weapId) {
    if (weapId == WEAPON_UNARMED) return true;
    if (weapId >= 60 || !GetWeaponInfo) return false;
    CWeaponInfo* pInfo = GetWeaponInfo(weapId, 1);
    if (!pInfo) return false;
    uint32_t fireType = *(uint32_t*)((uintptr_t)pInfo + 0x00);
    if (fireType != WEAPON_FIRE_INSTANT_HIT && fireType != WEAPON_FIRE_PROJECTILE) return false;
    return (gSettings.weaponRates[weapId].gangster >= 0.0f);
}

void CycleDrivebyWeapon(CPlayerPed* pPlayer, int dir) {
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

        if (curSlot == 0 || (totalAmmo > 0 && IsWeaponValidForDriveby(weapId))) {
            SetCurrentWeapon((CPed*)pPlayer, curSlot);
            g_fCachedPlayerShootRate = ComputePedDrivebyShootRate((CPed*)pPlayer);
            if (ReportFrontendAudioEvent && pAudioEngine) {
                ReportFrontendAudioEvent(pAudioEngine, 32, 1.0f, 1.0f);
            }
            break;
        }
    }
}

void ManageVehicleWindows(CVehicle* pVeh, bool bOpen) {
    if (!pVeh) return;
    int modelId = GetEntityModelIndex((CEntity*)pVeh);
    auto it = gSettings.carsWithoutWindows.find(modelId);
    if (it != gSettings.carsWithoutWindows.end() && it->second >= 1) return;

    eVehicleClass vClass = GetVehicleClass(pVeh);
    if (vClass != VEHICLE_AUTOMOBILE && vClass != VEHICLE_MTRUCK) return;

    if (bOpen && SetWindowOpenFlag) {
        SetWindowOpenFlag(pVeh, 8);
        SetWindowOpenFlag(pVeh, 10);
    } else if (!bOpen && gSettings.closeWindowsAgain && ClearWindowOpenFlag) {
        ClearWindowOpenFlag(pVeh, 8);
        ClearWindowOpenFlag(pVeh, 9);
        ClearWindowOpenFlag(pVeh, 10);
        ClearWindowOpenFlag(pVeh, 11);
    }
}

void StartOrStopDrivebyTask(CPlayerPed* pPlayer, CVehicle* pVeh, bool bStart) {
    if (!pPlayer) return;
    void* pPedIntelligence = *(void**)((uintptr_t)pPlayer + 0x570);
    if (!pPedIntelligence) return;
    void* pTaskMgr = (void*)((uintptr_t)pPedIntelligence + 0x08);

    if (bStart) {
        uint8_t curSlot = *(uint8_t*)((uintptr_t)pPlayer + 0x8E8);
        uintptr_t weaponBase = (uintptr_t)pPlayer + 0x710 + (curSlot * 0x20);
        eWeaponType activeWeap = *(eWeaponType*)(weaponBase + 0x00);

        if (activeWeap == WEAPON_UNARMED || !IsWeaponValidForDriveby(activeWeap)) {
            CycleDrivebyWeapon(pPlayer, 1);
        } else {
            g_fCachedPlayerShootRate = ComputePedDrivebyShootRate((CPed*)pPlayer);
        }

        ManageVehicleWindows(pVeh, true);

        if (OperatorNew && TaskDriveByCtor && SetTaskSecondary) {
            CTaskSimpleGangDriveBy* pTask = (CTaskSimpleGangDriveBy*)OperatorNew(128);
            if (pTask) {
                CVector zeroVec = {0.0f, 0.0f, 0.0f};
                TaskDriveByCtor(pTask, nullptr, &zeroVec, 600.0f, 100, 4, false);
                SetTaskSecondary(pTaskMgr, (CTask*)pTask, 1);
            }
        }
    } else {
        if (SetTaskSecondary) {
            SetTaskSecondary(pTaskMgr, nullptr, 1);
        }
        if (gSettings.jumpcutRestore > 0 && RestoreCameraJumpCut && pTheCamera) {
            RestoreCameraJumpCut(pTheCamera);
        }
    }
}

void ApplyDrivebyShootRate(CPed* pPed, float rateMult) {
    if (!pPed || !RpAnimBlendClumpGetAssociation || rateMult <= 0.0f) return;
    void* pRpClump = *(void**)((uintptr_t)pPed + 0x18);
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

// ============================================================================
// 6. ENGINE HOOKS (HUD ICON DRAWING + TOUCHSCREEN TAP + GAMEPLAY)
// ============================================================================

// Hook 1: Draw Cheeseburger's Driveby Icon on HUD when inside a vehicle
DECL_HOOKv(CHud_DrawAfterFade) {
    CHud_DrawAfterFade();

    if (!gSettings.enableMod || !gSettings.showTouchIcon || !g_pCurrentVeh) return;

    // Lazy-load the icon texture from "mobile" TXD (texdb/mobile/mobile.txt)
    if (!g_bSpriteLoaded && TxdFindSlot && TxdPushCurrent && TxdSetCurrent && TxdPopCurrent && SpriteSetTexture) {
        int32_t mobileSlot = TxdFindSlot("mobile");
        if (mobileSlot != -1) {
            TxdPushCurrent();
            TxdSetCurrent(mobileSlot);
            SpriteSetTexture(&g_DrivebyIconSprite, gSettings.iconTextureName);
            TxdPopCurrent();
        }
        g_bSpriteLoaded = true;
    }

    if (g_DrivebyIconSprite.m_pTexture && SpriteDraw) {
        CRect iconRect = GetTouchIconRect();
        // Semi-transparent when OFF (145 alpha), bright full-opacity when ON (255 alpha)
        CRGBA iconColor = g_bInManualDriveby ? CRGBA(255, 255, 255, 255) : CRGBA(255, 255, 255, 145);
        SpriteDraw(&g_DrivebyIconSprite, iconRect, iconColor);
    }
}

// Hook 2: Detect Finger Tap on the Driveby Icon
// action: 0 = Touch Down, 1 = Touch Up, 2 = Touch Move
DECL_HOOKv(AND_TouchEvent, int action, int pointerId, int x, int y) {
    if (gSettings.enableMod && gSettings.showTouchIcon && g_pCurrentVeh && action == 0) {
        CRect r = GetTouchIconRect();
        if ((float)x >= r.left && (float)x <= r.right &&
            (float)y >= r.top  && (float)y <= r.bottom) {
            g_bTouchToggleRequested = true;
            return; // Consume touch so it doesn't steer or fire accidentally
        }
    }
    AND_TouchEvent(action, pointerId, x, y);
}

DECL_HOOK(bool, CPad_GetLookLeft, CPad* self) {
    if (g_bInManualDriveby) return false;
    return CPad_GetLookLeft(self);
}

DECL_HOOK(bool, CPad_GetLookRight, CPad* self) {
    if (g_bInManualDriveby) return false;
    return CPad_GetLookRight(self);
}

DECL_HOOK(bool, CPad_GetLookBehindForCar, CPad* self) {
    if (g_bInManualDriveby) return false;
    return CPad_GetLookBehindForCar(self);
}

DECL_HOOK(bool, CTaskSimpleGangDriveBy_ProcessPed, CTaskSimpleGangDriveBy* self, CPed* pPed) {
    bool res = CTaskSimpleGangDriveBy_ProcessPed(self, pPed);

    CPlayerPed* pPlayer = FindPlayerPed ? FindPlayerPed(-1) : nullptr;
    if (pPed == (CPed*)pPlayer && g_bInManualDriveby) {
        ApplyDrivebyShootRate(pPed, g_fCachedPlayerShootRate);

        if (gSettings.windshieldMode > 0 && g_pCurrentVeh && DamageCarPanel) {
            int modelId = GetEntityModelIndex((CEntity*)g_pCurrentVeh);
            auto it = gSettings.carsWithoutWindows.find(modelId);
            if (it == gSettings.carsWithoutWindows.end() || it->second < 2) {
                int8_t animDir = *(int8_t*)((uintptr_t)self + 0x24);
                if (animDir == 0) DamageCarPanel(g_pCurrentVeh, 4);
            }
        }
    } else if (gSettings.fixShootRateOfNPC && pPed != (CPed*)pPlayer) {
        ApplyDrivebyShootRate(pPed, ComputePedDrivebyShootRate(pPed));
    }
    return res;
}

DECL_HOOKv(CGame_Process) {
    CGame_Process();

    if (!gSettings.enableMod || !FindPlayerPed || !FindPlayerVehicle) return;

    if (gSettings.preLoadIFP && !g_bIfpPreloaded && GetAnimationBlockIndex && RequestModel) {
        int32_t ifpIdx = GetAnimationBlockIndex("drivebys");
        if (ifpIdx != -1) {
            RequestModel(ifpIdx + 25557, 0x02);
            g_bIfpPreloaded = true;
        }
    }

    if (gSettings.disableOnMission && pOnAMissionFlag && *pOnAMissionFlag != 0) {
        g_bInManualDriveby = false;
        return;
    }

    CPlayerPed* pPlayer = FindPlayerPed(-1);
    CVehicle* pVeh = FindPlayerVehicle(-1, false);

    if (!pPlayer || !pVeh) {
        if (g_pLastActiveVeh && g_bInManualDriveby) {
            ManageVehicleWindows(g_pLastActiveVeh, false);
        }
        g_bInManualDriveby = false;
        g_bTouchToggleRequested = false;
        g_pCurrentVeh = nullptr;
        g_pLastActiveVeh = nullptr;
        return;
    }

    g_pCurrentVeh = pVeh;
    g_pLastActiveVeh = pVeh;

    CPad* pPad = GetPad ? GetPad(0) : nullptr;
    if (!pPad) return;

    bool bLookL = CPad_GetLookLeft(pPad);
    bool bLookR = CPad_GetLookRight(pPad);
    bool bComboPressed = (bLookL && bLookR);

    // Toggle via Touch Icon Tap OR Gamepad LookL+LookR Combo
    if (g_bTouchToggleRequested || (bComboPressed && !g_bPrevComboPressed)) {
        g_bTouchToggleRequested = false;
        g_bInManualDriveby = !g_bInManualDriveby;
        StartOrStopDrivebyTask(pPlayer, pVeh, g_bInManualDriveby);
        if (ReportFrontendAudioEvent && pAudioEngine) {
            ReportFrontendAudioEvent(pAudioEngine, 1, 1.0f, 1.0f);
        }
    }
    g_bPrevComboPressed = bComboPressed;

    // Keep Driveby Task active while toggled on
    if (g_bInManualDriveby && GetTaskSecondary) {
        void* pPedIntelligence = *(void**)((uintptr_t)pPlayer + 0x570);
        if (pPedIntelligence) {
            void* pTaskMgr = (void*)((uintptr_t)pPedIntelligence + 0x08);
            if (!GetTaskSecondary(pTaskMgr, 1)) {
                StartOrStopDrivebyTask(pPlayer, pVeh, true);
            }
        }
    }

    // Weapon Switching (Swipe Weapon Icon OR Controller Look Left/Right)
    bool bSwipeNext = (gSettings.allowTouchWeaponSwipe && PadCycleWeaponRight) ? PadCycleWeaponRight(pPad) : false;
    bool bSwipePrev = (gSettings.allowTouchWeaponSwipe && PadCycleWeaponLeft)  ? PadCycleWeaponLeft(pPad)  : false;

    if (g_bInManualDriveby && !bComboPressed) {
        if (bSwipeNext || (gSettings.sideLookButtons > 0 && bLookR && !bLookL)) {
            CycleDrivebyWeapon(pPlayer, 1);
            StartOrStopDrivebyTask(pPlayer, pVeh, true);
        } else if (bSwipePrev || (gSettings.sideLookButtons > 0 && bLookL && !bLookR)) {
            CycleDrivebyWeapon(pPlayer, -1);
            StartOrStopDrivebyTask(pPlayer, pVeh, true);
        }
    }
}

// ============================================================================
// 7. PLUGIN ENTRY POINT
// ============================================================================
extern "C" void OnModLoad() {
    logger->SetTag("ManualDrivebySA");

    InitAndSaveConfig();
    if (!gSettings.enableMod) return;

    hGTASA = aml->GetLibHandle("libGTASA.so");
    pGTASA = aml->GetLib("libGTASA.so");
    if (!hGTASA || !pGTASA) return;

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
    SET_TO(PadCycleWeaponRight,      aml->GetSym(hGTASA, "_ZN4CPad20CycleWeaponRightJustDownEv"));
    SET_TO(PadCycleWeaponLeft,       aml->GetSym(hGTASA, "_ZN4CPad19CycleWeaponLeftJustDownEv"));

    // Sprite & TXD Store Symbols
    SET_TO(TxdFindSlot,              aml->GetSym(hGTASA, "_ZN9CTxdStore11FindTxdSlotEPKc"));
    SET_TO(TxdPushCurrent,           aml->GetSym(hGTASA, "_ZN9CTxdStore14PushCurrentTxdEv"));
    SET_TO(TxdPopCurrent,            aml->GetSym(hGTASA, "_ZN9CTxdStore13PopCurrentTxdEv"));
    SET_TO(TxdSetCurrent,            aml->GetSym(hGTASA, "_ZN9CTxdStore13SetCurrentTxdEi"));
    SET_TO(SpriteSetTexture,         aml->GetSym(hGTASA, "_ZN9CSprite2d10SetTextureEPc"));
    SET_TO(SpriteDraw,               aml->GetSym(hGTASA, "_ZN9CSprite2d4DrawERK5CRectRK5CRGBA"));

    SET_TO(OperatorNew,              aml->GetSym(hGTASA, "_Znwm"));
    SET_TO(TaskDriveByCtor,          aml->GetSym(hGTASA, "_ZN22CTaskSimpleGangDriveByC2EP7CEntityPK7CVectorfaab"));
    SET_TO(SetTaskSecondary,         aml->GetSym(hGTASA, "_ZN12CTaskManager16SetTaskSecondaryEP5CTaski"));
    SET_TO(GetTaskSecondary,         aml->GetSym(hGTASA, "_ZN12CTaskManager16GetTaskSecondaryEi"));
    SET_TO(RpAnimBlendClumpGetAssociation, aml->GetSym(hGTASA, "_Z30RpAnimBlendClumpGetAssociationP7RpClumpPKc"));

    SET_TO(pTheCamera,               aml->GetSym(hGTASA, "TheCamera"));
    SET_TO(pAudioEngine,             aml->GetSym(hGTASA, "AEAudioHardware"));
    SET_TO(pTimeInMilliseconds,      aml->GetSym(hGTASA, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(pOnAMissionFlag,          aml->GetSym(hGTASA, "OnAMissionFlag"));

    void* pRsGlobal = aml->GetSym(hGTASA, "RsGlobal");
    if (pRsGlobal) {
        // In 64-bit RsGlobalType, maximumWidth is at +0x08 and maximumHeight is at +0x0C
        pRsGlobalWidth  = (int32_t*)((uintptr_t)pRsGlobal + 0x08);
        pRsGlobalHeight = (int32_t*)((uintptr_t)pRsGlobal + 0x0C);
    }

    HOOKSYM(CHud_DrawAfterFade,                hGTASA, "_ZN4CHud13DrawAfterFadeEv");
    HOOKSYM(AND_TouchEvent,                    hGTASA, "_Z14AND_TouchEventiiii");
    HOOKSYM(CPad_GetLookLeft,                  hGTASA, "_ZN4CPad11GetLookLeftEv");
    HOOKSYM(CPad_GetLookRight,                 hGTASA, "_ZN4CPad12GetLookRightEv");
    HOOKSYM(CPad_GetLookBehindForCar,          hGTASA, "_ZN4CPad19GetLookBehindForCarEv");
    HOOKSYM(CTaskSimpleGangDriveBy_ProcessPed, hGTASA, "_ZN22CTaskSimpleGangDriveBy10ProcessPedEP4CPed");
    HOOKSYM(CGame_Process,                     hGTASA, "_ZN5CGame7ProcessEv");

    logger->Info("Manual Driveby Refixed v1.3 with Touch Icon loaded!");
}
