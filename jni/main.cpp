#include <mod/amlmod.h>
#include <mod/logger.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <unordered_map>
#include <string>

// Using MYMOD + native DrivebySettings.ini auto-generator avoids the Config::Config linker dependency
// while generating the exact commented DrivebySettings.ini file in /configs/!
MYMOD(net.PS2Revival.ManualDriveby, GTA:SA Manual Driveby Refixed, 1.4, devil_desi)

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
// 2. SETTINGS STRUCT & DEFAULT INI TEMPLATE
// ============================================================================
struct SkillRates {
    float poor = -2.0f;
    float gangster = -2.0f;
    float hitman = -2.0f;
};

struct DrivebySettings {
    // [MAIN]
    bool enableMod = true;
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

    // [TOUCH_ICON]
    bool showTouchIcon = true;
    char iconTextureName[64] = "hud_driveby";
    float iconPosXRatio = 0.84f;
    float iconPosYRatio = 0.45f;
    float iconSizePx = 135.0f;

    // [WEAPONSWITCH]
    bool allowTouchWeaponSwipe = true;
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
    uint8_t classMaskCars   = 0b1110;
    uint8_t classMaskBikes  = 0b1110;
    uint8_t classMaskBoats  = 0b1110;
    uint8_t classMaskHelis  = 0b1110;
    uint8_t classMaskPlanes = 0b1110;
    uint8_t classMaskTrains = 0b1110;
    std::unordered_map<int, uint8_t> modelExceptions;

    // [CARSWITHOUTWINDOWS]
    std::unordered_map<int, int> carsWithoutWindows;
} gSettings;

static const char* kDefaultIniContent = R"INI(; ------------------------ Manual Driveby Refixed (AML 2.10 Android) -----------------------

[MAIN]
EnableMod = 1
DrivebyControlType = 1      ; Gamepad: 1 = L1+R1 / LookL+LookR toggle, 2 = Hold to aim
PreLoadIFP = 1              ; Keeps drivebys.ifp preloaded in memory
DisableBikeWeakPoint = 1    ; Protects bike gas tank while aiming
NoDrivingWhileAiming = 0    ; 0 = Off, 1 = All vehicles, 2 = Only bikes
PrepareThrowAnim = 1
CloseWindowsAgain = 1       ; Rolls up undamaged car windows when exiting
WindshieldMode = 1          ; 1 = Breaks windshield when shooting forward
FixPedReactions = 2
RecruitBlip = 1
CrosshairExpMult = 0.75

[TOUCH_ICON]
ShowTouchIcon = 1           ; Draws the driveby toggle button on screen when inside a vehicle
IconTextureName = hud_driveby ; Texture name inside texdb/mobile/mobile.txt
IconPosXRatio = 0.84        ; Horizontal position (0.0 = left, 1.0 = right)
IconPosYRatio = 0.45        ; Vertical position (0.0 = top, 1.0 = bottom)
IconSizePx = 135.0          ; Icon size in pixels (scaled to 1080p height)

[WEAPONSWITCH]
AllowTouchWeaponSwipe = 1   ; Swipe top-right weapon widget to switch weapons in car
DPadButtonsMode = 1         ; 0 = Off, 1 = Left/Right, 2 = Up/Down
SideLookButtons = 1         ; 0 = Off, 1 = On, 2 = Only while aiming
EnableOnStory = 1

[WEAPONSETTINGS]
FixShootRateOfNPC = 1
Default = 1.00 / 1.00 / 1.00

16 = 0.32 / 1.00 / 0.70     ; Grenade
17 = 0.08 / 1.00 / 0.70     ; Teargas
18 = 0.18 / 1.00 / 0.60     ; Molotov
39 = 0.06 / 1.00 / 0.40     ; Satchel
40 = 0.00 / 1.00 / 0.00     ; Detonator
37 = 0.00 / 1.00 / 0.00     ; Flamethrower
41 = 0.00 / 1.00 / 0.00     ; Spraycan
42 = 0.00 / 1.00 / 0.00     ; Fire Extinguisher
43 = 0.00 / 1.00 / 0.00     ; Camera

[CAMERA]
DistCar = 5.00 / 10.0 / 17.0
DistBike = 2.80 / 4.40 / 17.3
HeightCar = 0.50 / 0.55 / 0.60
HeightBike = 0.20 / 0.20 / 0.20
TweakFOV = 55.0 / 60.0 / 65.0
RestoreDistance = 0
JumpcutRestore = 1
JumpcutCarSpeed = 8.0
KeepAiming = 0
FreeLineOfSight = 1
NoAngleLimit = 1

[EXCEPTIONS]
DisableOnMission = 0
Cars = 1/1/1
Bikes = 1/1/1
Boats = 1/1/1
Helis = 1/1/1
Planes = 1/1/1
Trains = 1/1/1
407 = 0/0/0                 ; Firetruck

[CARSWITHOUTWINDOWS]
406 = 2
530 = 2
485 = 2
568 = 1
457 = 1
533 = 1
439 = 1
429 = 1
480 = 1
555 = 1
536 = 1
575 = 1
567 = 1
592 = 1
577 = 1
553 = 1
519 = 1
)INI";

// Runtime State
static bool g_bInManualDriveby = false;
static bool g_bPrevComboPressed = false;
static bool g_bTouchToggleRequested = false;
static bool g_bIfpPreloaded = false;
static bool g_bSpriteLoaded = false;
static CSprite2d g_DrivebyIconSprite;
static uint32_t g_nLastSwitchTick = 0;
static uint32_t g_nLastTouchToggleTick = 0;
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

// Texture, Sprite2d & RenderState Functions
int32_t     (*TxdFindSlot)(const char* name) = nullptr;
void        (*TxdPushCurrent)(void) = nullptr;
void        (*TxdPopCurrent)(void) = nullptr;
void        (*TxdSetCurrent)(int32_t slot) = nullptr;
void        (*SpriteSetTexture)(CSprite2d* self, const char* name) = nullptr;
void        (*SpriteDraw)(CSprite2d* self, const CRect& rect, const CRGBA& color) = nullptr;
void        (*RwRenderStateSet)(int32_t state, void* value) = nullptr;

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
// 4. INI GENERATOR & PARSER
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

void LoadOrGenerateDrivebyINI() {
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
        // File does not exist -> Auto-generate it in /configs/DrivebySettings.ini!
        FILE* fw = std::fopen(iniPath, "w");
        if (fw) {
            std::fputs(kDefaultIniContent, fw);
            std::fclose(fw);
            logger->Info("Generated default DrivebySettings.ini at %s", iniPath);
        }
        fp = std::fopen(iniPath, "r");
    }

    if (!fp) {
        logger->Error("Could not open or create DrivebySettings.ini!");
        return;
    }

    std::string currentSection;
    char lineBuf[512];
    while (std::fgets(lineBuf, sizeof(lineBuf), fp)) {
        std::string line(lineBuf);

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
            if (key == "EnableMod")                 gSettings.enableMod = (std::atoi(val.c_str()) > 0);
            else if (key == "DrivebyControlType")   gSettings.drivebyControlType = std::atoi(val.c_str());
            else if (key == "PreLoadIFP")           gSettings.preLoadIFP = (std::atoi(val.c_str()) > 0);
            else if (key == "DisableBikeWeakPoint") gSettings.disableBikeWeakPoint = (std::atoi(val.c_str()) > 0);
            else if (key == "NoDrivingWhileAiming") gSettings.noDrivingWhileAiming = std::atoi(val.c_str());
            else if (key == "PrepareThrowAnim")     gSettings.prepareThrowAnim = (std::atoi(val.c_str()) > 0);
            else if (key == "CloseWindowsAgain")    gSettings.closeWindowsAgain = (std::atoi(val.c_str()) > 0);
            else if (key == "WindshieldMode")       gSettings.windshieldMode = std::atoi(val.c_str());
            else if (key == "FixPedReactions")      gSettings.fixPedReactions = std::atoi(val.c_str());
            else if (key == "RecruitBlip")          gSettings.recruitBlip = (std::atoi(val.c_str()) > 0);
            else if (key == "CrosshairExpMult")     gSettings.crosshairExpMult = std::strtof(val.c_str(), nullptr);
        }
        else if (currentSection == "TOUCH_ICON") {
            if (key == "ShowTouchIcon")             gSettings.showTouchIcon = (std::atoi(val.c_str()) > 0 || val == "true");
            else if (key == "IconTextureName")      std::snprintf(gSettings.iconTextureName, sizeof(gSettings.iconTextureName), "%s", val.c_str());
            else if (key == "IconPosXRatio")        gSettings.iconPosXRatio = std::strtof(val.c_str(), nullptr);
            else if (key == "IconPosYRatio")        gSettings.iconPosYRatio = std::strtof(val.c_str(), nullptr);
            else if (key == "IconSizePx")           gSettings.iconSizePx = std::strtof(val.c_str(), nullptr);
        }
        else if (currentSection == "WEAPONSWITCH") {
            if (key == "AllowTouchWeaponSwipe")     gSettings.allowTouchWeaponSwipe = (std::atoi(val.c_str()) > 0 || val == "true");
            else if (key == "DPadButtonsMode")      gSettings.dpadButtonsMode = std::atoi(val.c_str());
            else if (key == "SideLookButtons")      gSettings.sideLookButtons = std::atoi(val.c_str());
            else if (key == "EnableOnStory")        gSettings.enableOnStory = (std::atoi(val.c_str()) > 0);
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
            if (key == "DistCar")              std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.distCar[0], &gSettings.distCar[1], &gSettings.distCar[2]);
            else if (key == "DistBike")        std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.distBike[0], &gSettings.distBike[1], &gSettings.distBike[2]);
            else if (key == "HeightCar")       std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.heightCar[0], &gSettings.heightCar[1], &gSettings.heightCar[2]);
            else if (key == "HeightBike")      std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.heightBike[0], &gSettings.heightBike[1], &gSettings.heightBike[2]);
            else if (key == "TweakFOV")        std::sscanf(val.c_str(), "%f / %f / %f", &gSettings.tweakFOV[0], &gSettings.tweakFOV[1], &gSettings.tweakFOV[2]);
            else if (key == "RestoreDistance") gSettings.restoreDistance = std::atoi(val.c_str());
            else if (key == "JumpcutRestore")  gSettings.jumpcutRestore = std::atoi(val.c_str());
            else if (key == "JumpcutCarSpeed") gSettings.jumpcutCarSpeed = std::strtof(val.c_str(), nullptr);
            else if (key == "KeepAiming")      gSettings.keepAiming = std::atoi(val.c_str());
            else if (key == "FreeLineOfSight") gSettings.freeLineOfSight = (std::atoi(val.c_str()) > 0);
            else if (key == "NoAngleLimit")    gSettings.noAngleLimit = (std::atoi(val.c_str()) > 0);
        }
        else if (currentSection == "EXCEPTIONS") {
            if (key == "DisableOnMission") gSettings.disableOnMission = (std::atoi(val.c_str()) > 0);
            else if (key == "Cars")        gSettings.classMaskCars   = ParseExceptionMask(val.c_str());
            else if (key == "Bikes")       gSettings.classMaskBikes  = ParseExceptionMask(val.c_str());
            else if (key == "Boats")       gSettings.classMaskBoats  = ParseExceptionMask(val.c_str());
            else if (key == "Helis")       gSettings.classMaskHelis  = ParseExceptionMask(val.c_str());
            else if (key == "Planes")      gSettings.classMaskPlanes = ParseExceptionMask(val.c_str());
            else if (key == "Trains")      gSettings.classMaskTrains = ParseExceptionMask(val.c_str());
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
    logger->Info("DrivebySettings.ini parsed successfully!");
}

// ============================================================================
// 5. ENGINE HELPER FUNCTIONS
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

    // Note: CSprite2d::Draw expects CRect(left, bottom, right, top) where top < bottom in screen space
    CRect r;
    r.left   = centerX - halfSize;
    r.right  = centerX + halfSize;
    r.top    = centerY - halfSize;
    r.bottom = centerY + halfSize;
    return r;
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

float ComputePedDrivebyShootRate(CPed* pPed) {
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

    const SkillRates& sr = gSettings.weaponRates[weapId];
    float rateMult = (skill == 0) ? sr.poor : ((skill == 1) ? sr.gangster : sr.hitman);
    if (rateMult == -2.0f) {
        rateMult = (skill == 0) ? gSettings.defaultRates.poor :
                   ((skill == 1) ? gSettings.defaultRates.gangster : gSettings.defaultRates.hitman);
    }

    if (rateMult <= 0.0f) return 0.0f;
    if (rateMult > 9.99f) rateMult = 1.0f;

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

bool IsWeaponSelectableForDriveby(CVehicle* pVeh, eWeaponType weapId) {
    if (weapId == WEAPON_UNARMED) return true;
    if (weapId >= 256 || !GetWeaponInfo) return false;

    CWeaponInfo* pInfo = GetWeaponInfo(weapId, 1);
    if (!pInfo) return false;

    uint32_t fireType = *(uint32_t*)((uintptr_t)pInfo + 0x00);
    if (fireType == WEAPON_FIRE_CAMERA) fireType = WEAPON_FIRE_INSTANT_HIT;
    if (fireType < 1 || fireType > 3) return false;

    if (weapId != WEAPON_MICRO_UZI && weapId != WEAPON_MP5 && weapId != WEAPON_TEC9) {
        uint8_t allowedMask = GetAllowedWeaponMaskForVehicle(pVeh);
        if (!(allowedMask & (1 << fireType))) return false;
    }

    const SkillRates& sr = gSettings.weaponRates[weapId];
    float cfgVal = (sr.gangster == -2.0f) ? gSettings.defaultRates.gangster : sr.gangster;
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

        if (curSlot == 0 || (totalAmmo > 0 && IsWeaponSelectableForDriveby(pVeh, weapId))) {
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

        if (activeWeap == WEAPON_UNARMED || !IsWeaponSelectableForDriveby(pVeh, activeWeap)) {
            CycleDrivebyWeapon(pPlayer, pVeh, 1);
        } else {
            g_fCachedPlayerShootRate = ComputePedDrivebyShootRate((CPed*)pPlayer);
        }

        ManageVehicleWindows(pVeh, true);

        if (OperatorNew && TaskDriveByCtor && SetTaskSecondary) {
            CTaskSimpleGangDriveBy* pTask = (CTaskSimpleGangDriveBy*)OperatorNew(128);
            if (pTask) {
                CVector zeroVec = {0.0f, 0.0f, 0.0f};
                TaskDriveByCtor(pTask, nullptr, &zeroVec, 600.0f, 100, 4, false); // Style 4 = 360 Free-Aim
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
// 6. ENGINE HOOKS
// ============================================================================

// Draw Cheeseburger's Driveby Icon with Alpha Blending enabled
DECL_HOOKv(CHud_DrawAfterFade) {
    CHud_DrawAfterFade();

    if (!gSettings.enableMod || !gSettings.showTouchIcon || !g_pCurrentVeh) return;

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
        if (RwRenderStateSet) {
            // rwRENDERSTATEVERTEXALPHAENABLE = 12, rwRENDERSTATESRCBLEND = 10 (5=SRCALPHA), rwRENDERSTATEDESTBLEND = 11 (6=INVSRCALPHA)
            RwRenderStateSet(12, (void*)1);
            RwRenderStateSet(10, (void*)5);
            RwRenderStateSet(11, (void*)6);
        }
        CRect iconRect = GetTouchIconRect();
        CRGBA iconColor = g_bInManualDriveby ? CRGBA(255, 255, 255, 255) : CRGBA(255, 255, 255, 145);
        SpriteDraw(&g_DrivebyIconSprite, iconRect, iconColor);
    }
}

// Intercept Touch Down on the Driveby Icon
DECL_HOOKv(AND_TouchEvent, int action, int pointerId, int x, int y) {
    if (gSettings.enableMod && gSettings.showTouchIcon && g_pCurrentVeh && action == 0) {
        CRect r = GetTouchIconRect();
        if ((float)x >= r.left && (float)x <= r.right &&
            (float)y >= r.top  && (float)y <= r.bottom) {
            uint32_t now = GetTicks();
            if (now - g_nLastTouchToggleTick > 250) {
                g_bTouchToggleRequested = true;
                g_nLastTouchToggleTick = now;
            }
            return;
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

    if (g_bTouchToggleRequested || (bComboPressed && !g_bPrevComboPressed)) {
        g_bTouchToggleRequested = false;
        g_bInManualDriveby = !g_bInManualDriveby;
        StartOrStopDrivebyTask(pPlayer, pVeh, g_bInManualDriveby);
        if (ReportFrontendAudioEvent && pAudioEngine) {
            ReportFrontendAudioEvent(pAudioEngine, 1, 1.0f, 1.0f);
        }
    }
    g_bPrevComboPressed = bComboPressed;

    if (g_bInManualDriveby && GetTaskSecondary) {
        void* pPedIntelligence = *(void**)((uintptr_t)pPlayer + 0x570);
        if (pPedIntelligence) {
            void* pTaskMgr = (void*)((uintptr_t)pPedIntelligence + 0x08);
            if (!GetTaskSecondary(pTaskMgr, 1)) {
                StartOrStopDrivebyTask(pPlayer, pVeh, true);
            }
        }
    }

    bool bSwipeNext = (gSettings.allowTouchWeaponSwipe && PadCycleWeaponRight) ? PadCycleWeaponRight(pPad) : false;
    bool bSwipePrev = (gSettings.allowTouchWeaponSwipe && PadCycleWeaponLeft)  ? PadCycleWeaponLeft(pPad)  : false;

    if (g_bInManualDriveby && !bComboPressed) {
        if (bSwipeNext || (gSettings.sideLookButtons > 0 && bLookR && !bLookL)) {
            CycleDrivebyWeapon(pPlayer, pVeh, 1);
            StartOrStopDrivebyTask(pPlayer, pVeh, true);
        } else if (bSwipePrev || (gSettings.sideLookButtons > 0 && bLookL && !bLookR)) {
            CycleDrivebyWeapon(pPlayer, pVeh, -1);
            StartOrStopDrivebyTask(pPlayer, pVeh, true);
        }
    }
}

// ============================================================================
// 7. PLUGIN INITIALIZATION
// ============================================================================
extern "C" void OnModLoad() {
    logger->SetTag("ManualDrivebySA");

    // 1. Auto-generate (if deleted) and parse /configs/DrivebySettings.ini
    LoadOrGenerateDrivebyINI();

    if (!gSettings.enableMod) {
        logger->Info("Manual Driveby disabled via DrivebySettings.ini.");
        return;
    }

    hGTASA = aml->GetLibHandle("libGTASA.so");
    pGTASA = aml->GetLib("libGTASA.so");
    if (!hGTASA || !pGTASA) {
        logger->Error("libGTASA.so handle not found!");
        return;
    }

    // 2. Resolve Engine Symbols
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

    // Sprite, TXD & RenderState Symbols
    SET_TO(TxdFindSlot,              aml->GetSym(hGTASA, "_ZN9CTxdStore11FindTxdSlotEPKc"));
    SET_TO(TxdPushCurrent,           aml->GetSym(hGTASA, "_ZN9CTxdStore14PushCurrentTxdEv"));
    SET_TO(TxdPopCurrent,            aml->GetSym(hGTASA, "_ZN9CTxdStore13PopCurrentTxdEv"));
    SET_TO(TxdSetCurrent,            aml->GetSym(hGTASA, "_ZN9CTxdStore13SetCurrentTxdEi"));
    SET_TO(SpriteSetTexture,         aml->GetSym(hGTASA, "_ZN9CSprite2d10SetTextureEPc"));
    SET_TO(SpriteDraw,               aml->GetSym(hGTASA, "_ZN9CSprite2d4DrawERK5CRectRK5CRGBA"));
    SET_TO(RwRenderStateSet,         aml->GetSym(hGTASA, "_Z16RwRenderStateSet13RwRenderStatePv"));

    SET_TO(OperatorNew,              aml->GetSym(hGTASA, "_Znwm"));
    SET_TO(TaskDriveByCtor,          aml->GetSym(hGTASA, "_ZN22CTaskSimpleGangDriveByC2EP7CEntityPK7CVectorfaab"));
    SET_TO(SetTaskSecondary,         aml->GetSym(hGTASA, "_ZN12CTaskManager16SetTaskSecondaryEP5CTaski"));
    SET_TO(GetTaskSecondary,         aml->GetSym(hGTASA, "_ZN12CTaskManager16GetTaskSecondaryEi"));
    SET_TO(RpAnimBlendClumpGetAssociation, aml->GetSym(hGTASA, "_Z30RpAnimBlendClumpGetAssociationP7RpClumpPKc"));

    SET_TO(pTheCamera,               aml->GetSym(hGTASA, "TheCamera"));
    SET_TO(pAudioEngine,             aml->GetSym(hGTASA, "AEAudioHardware"));
    SET_TO(pTimeInMilliseconds,      aml->GetSym(hGTASA, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(pOnAMissionFlag,          aml->GetSym(hGTASA, "OnAMissionFlag"));

    // Fixed uintptr_t return type from aml->GetSym()
    uintptr_t pRsGlobal = aml->GetSym(hGTASA, "RsGlobal");
    if (pRsGlobal) {
        pRsGlobalWidth  = (int32_t*)(pRsGlobal + 0x08);
        pRsGlobalHeight = (int32_t*)(pRsGlobal + 0x0C);
    }

    // 3. Install ARM64 Hooks
    HOOKSYM(CHud_DrawAfterFade,                hGTASA, "_ZN4CHud13DrawAfterFadeEv");
    HOOKSYM(AND_TouchEvent,                    hGTASA, "_Z14AND_TouchEventiiii");
    HOOKSYM(CPad_GetLookLeft,                  hGTASA, "_ZN4CPad11GetLookLeftEv");
    HOOKSYM(CPad_GetLookRight,                 hGTASA, "_ZN4CPad12GetLookRightEv");
    HOOKSYM(CPad_GetLookBehindForCar,          hGTASA, "_ZN4CPad19GetLookBehindForCarEv");
    HOOKSYM(CPad_GetSteeringLeftRight,         hGTASA, "_ZN4CPad20GetSteeringLeftRightEv");
    HOOKSYM(CTaskSimpleGangDriveBy_ProcessPed, hGTASA, "_ZN22CTaskSimpleGangDriveBy10ProcessPedEP4CPed");
    HOOKSYM(CGame_Process,                     hGTASA, "_ZN5CGame7ProcessEv");

    logger->Info("Manual Driveby Refixed v1.4 (64-bit AML 2.10) loaded successfully!");
}
