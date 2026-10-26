#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#if !defined(IAML_VER) || IAML_VER < 01040000
    #error "You need AML 1.4.0 headers (IAML_VER >= 01040000)!"
#endif

#include "scripting.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// Bind directly to /configs/DrivebySettings.ini using MYMODCFGNAME
MYMODCFGNAME(net.qwertz.manualdriveby, Manual Driveby Refixed, 1.4, Qwertz, DrivebySettings)
NEEDGAME(com.rockstargames.gtasa)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.4.0)
END_DEPLIST()

// ============================================================================
// Engine Structs (64-bit ARM64 Layout)
// ============================================================================
struct CVector {
    float x, y, z;
};

struct CRGBA {
    uint8_t r, g, b, a;
};

struct CRect {
    float left, bottom, right, top;
};

struct RwTexture;
struct CSprite2d {
    RwTexture* m_pTexture;
};

struct CWeapon {
    uint32_t m_eWeaponType;
    uint32_t m_eState;
    uint32_t m_nAmmoInClip;
    uint32_t m_nTotalAmmo;
    uint32_t m_nTimeForNextShot;
    uint8_t  m_bFirstPersonWeaponModeSelected;
    uint8_t  m_bDontPlaceInHand;
    uint8_t  pad[2];
    void*    m_pFxSystem;
};

struct CWeaponInfo {
    uint32_t m_eFireType;        // 0x00
    float    m_fTargetRange;     // 0x04
    float    m_fWeaponRange;     // 0x08
    int32_t  m_nModelId1;        // 0x0C
    int32_t  m_nModelId2;        // 0x10
    uint32_t m_nSlot;            // 0x14
    uint32_t m_nFlags;           // 0x18
    uint32_t m_nAnimGroup;       // 0x1C
    uint16_t m_nAmmoClip;        // 0x20
    uint16_t m_nDamage;          // 0x22
    CVector  m_vecFireOffset;    // 0x24
    uint32_t m_nSkillLevel;      // 0x30
    uint32_t m_nReqStatLevel;    // 0x34
    float    m_fAccuracy;        // 0x38
    float    m_fMoveSpeed;       // 0x3C
    float    m_fAnimLoopStart;   // 0x40
    float    m_fAnimLoopEnd;     // 0x44
    float    m_fAnimLoopFire;    // 0x48
    float    m_fAnimLoop2Start;  // 0x4C
    float    m_fAnimLoop2End;    // 0x50
    float    m_fAnimLoop2Fire;   // 0x54
};

struct CControllerState {
    int16_t LeftStickX, LeftStickY;
    int16_t RightStickX, RightStickY;
    int16_t LeftShoulder1, LeftShoulder2;
    int16_t RightShoulder1, RightShoulder2;
    int16_t DPadUp, DPadDown, DPadLeft, DPadRight;
    int16_t Start, Select;
    int16_t ButtonSquare, ButtonTriangle, ButtonCross, ButtonCircle;
    int16_t ShockButtonL, ShockButtonR;
    int16_t m_bChatIndicated, m_bPedWalk, m_bVehicleMouseLook, m_bRadioTrackSkip;
};

struct CPad {
    CControllerState NewState;
    CControllerState OldState;
    int16_t SteeringLeftRightBuffer[10];
    int32_t DrunkDrivingBufferUsed;
    CControllerState PCTempKeyState;
    CControllerState PCTempJoyState;
    CControllerState PCTempMouseState;
    uint8_t Phase;
};

// ============================================================================
// Engine Pointers & Resolved Symbols
// ============================================================================
void* hGTASA = nullptr;
uintptr_t pGTASA = 0;

int8_t (*ProcessOneCommand)(CRunningScript* pScript) = nullptr;
SCRIPT_VAR* ScriptParams = nullptr;

CPad* ThePads = nullptr;
void* TheCamera = nullptr;
void** pIgnoreEntity = nullptr;
uint32_t* m_snTimeInMilliseconds = nullptr;
void* pAudioEngine = nullptr;

uintptr_t (*FindPlayerPed)(int playerId) = nullptr;
uintptr_t (*FindPlayerVehicle)(int playerId, bool bIncludeRemote) = nullptr;
int (*GetPedRef)(uintptr_t ped) = nullptr;
int (*GetVehicleRef)(uintptr_t veh) = nullptr;
uintptr_t (*GetPedFromRef)(int handle) = nullptr;
void (*GetBonePosition)(uintptr_t ped, CVector& outVec, uint32_t boneId, bool bIncludeAnim) = nullptr;
void (*Find3rdPersonCamTargetVector)(void* cam, float range, CVector src, CVector& outCam, CVector& outTarget) = nullptr;
void (*SetCurrentWeapon)(uintptr_t ped, int weaponType) = nullptr;
CWeaponInfo* (*GetWeaponInfo)(int weaponType, int8_t skill) = nullptr;
int8_t (*GetWeaponSkill)(uintptr_t ped) = nullptr;
bool (*FireAreaEffect)(CWeapon* weapon, uintptr_t firingEntity, CVector* origin, uintptr_t targetEntity, CVector* target) = nullptr;
bool (*AddProjectile)(uintptr_t creator, uint32_t weaponType, CVector pos, float force, CVector* dir, uintptr_t victim) = nullptr;
bool (*MakeThisPedJoinOurGroup)(uintptr_t playerPed, uintptr_t targetPed) = nullptr;
void (*StartOffensiveGangWar)() = nullptr;
RwTexture* (*GetTextureFromDB)(const char* texName) = nullptr;
void (*DrawSprite)(CSprite2d* sprite, CRect const& rect, CRGBA const& color) = nullptr;
void (*ReportFrontendAudioEvent)(void* audioEngine, int eventId, float vol, float speed) = nullptr;

// ============================================================================
// Configuration & Runtime State
// ============================================================================
struct DrivebyConfig {
    int DrivebyControlType = 1;
    int DPadButtonsMode = 1;
    int SideLookButtons = 0;
    int KeepAiming = 1;
    bool DisableBikeWeakPoint = true;
    bool CloseWindowsAgain = true;
    bool FreeLineOfSight = true;
    bool DisableOnMission = false;
    int FPCameraCentering = 1;
    bool EnableOnStory = true;
    bool NoAngleLimit = true;
    bool PrepareThrowAnim = true;
    int NoDrivingWhileAiming = 0;
    int WindshieldMode = 1;
    bool PreLoadIFP = true;
    float CrosshairExpMult = 1.0f;

    // Camera settings
    int RestoreDistance = 0;
    int JumpcutRestore = 0;
    float JumpcutCarSpeed = 50.0f;
    float FOV[3]        = { 70.0f, 70.0f, 70.0f };
    float DistBike[3]   = { 3.5f, 3.5f, 3.5f };
    float HeightBike[3] = { 0.2f, 0.2f, 0.2f };
    float DistCar[3]    = { 6.0f, 6.0f, 6.0f };
    float HeightCar[3]  = { 0.4f, 0.4f, 0.4f };

    // NPC Fixes
    bool FixShootRateOfNPC = true;
    int FixPedReactions = 2;
    bool RecruitBlip = true;

    // Touch Icon UI
    float TouchBtnX = 0.82f;
    float TouchBtnY = 0.42f;
    float TouchBtnSize = 0.085f;

    // Weapon Shoot Rates
    float WeaponShootRates[256][3];
    float DefaultShootRates[3] = { 1.0f, 1.0f, 1.0f };
    uint8_t VehicleSubclassFlags[6];
} gCfg;

struct RuntimeState {
    bool isAimingDriveby = false;
    bool isSpecialActionActive = false;
    bool ifpLoaded = false;
    uintptr_t lastVehicle = 0;
    int activeWeaponSlot = 0;
    int activeWeaponType = 0;
    float currentShootRate = 1.0f;
    float savedFov = 70.0f;

    // Spray & Throw Explosives state
    int tempObjectHandle = -1;
    int tempFxHandle = -1;
    uint32_t holdStartTime = 0;
    bool isHoldingThrow = false;

    // DBYFNPC State
    int targetedPedHandle = -1;
    int lastReactedPedHandle = -1;
    int recruitMarkerHandle = -1;
    int recruitedHomies[3] = { -1, -1, -1 };
    uint32_t lastPlayerShotTime = 0;

    CSprite2d toggleIconSprite = { nullptr };
} gState;

// All 16 car & bike driveby animations from MANDRBY & DBYFNPC
static const char* kDrivebyAnims[16] = {
    "gang_drivebylhs", "gang_drivebyrhs", "gang_drivebylhs_fwd", "gang_drivebylhs_bwd",
    "gang_drivebytop_lhs", "gang_drivebytop_rhs", "gang_drivebyrhs_fwd", "gang_drivebyrhs_bwd",
    "pass_driveby_lhs", "pass_driveby_rhs", "pass_driveby_fwd", "pass_driveby_bwd",
    "cop_dvby_l", "cop_dvby_r", "cop_dvby_ft", "cop_dvby_b"
};

// ============================================================================
// Configuration Loader (Uses AML 1.4.0 Fast Getters)
// ============================================================================
void LoadSettingsFromINI() {
    gCfg.DrivebyControlType   = cfg->GetInt("DrivebyControlType", 1, "MAIN");
    gCfg.DisableBikeWeakPoint = cfg->GetBool("DisableBikeWeakPoint", true, "MAIN");
    gCfg.CloseWindowsAgain    = cfg->GetBool("CloseWindowsAgain", true, "MAIN");
    gCfg.PrepareThrowAnim     = cfg->GetBool("PrepareThrowAnim", true, "MAIN");
    gCfg.NoDrivingWhileAiming = clampint(0, 2, cfg->GetInt("NoDrivingWhileAiming", 0, "MAIN"));
    gCfg.WindshieldMode       = clampint(0, 2, cfg->GetInt("WindshieldMode", 1, "MAIN"));
    gCfg.PreLoadIFP           = cfg->GetBool("PreLoadIFP", true, "MAIN");
    gCfg.CrosshairExpMult     = clampfloat(0.0f, 10.0f, cfg->GetFloat("CrosshairExpMult", 1.0f, "MAIN"));
    gCfg.FixPedReactions      = cfg->GetInt("FixPedReactions", 2, "MAIN");
    gCfg.RecruitBlip          = cfg->GetBool("RecruitBlip", true, "MAIN");

    gCfg.DPadButtonsMode      = cfg->GetInt("DPadButtonsMode", 1, "WEAPONSWITCH");
    gCfg.SideLookButtons      = cfg->GetInt("SideLookButtons", 0, "WEAPONSWITCH");
    gCfg.EnableOnStory        = cfg->GetBool("EnableOnStory", true, "WEAPONSWITCH");

    gCfg.KeepAiming           = cfg->GetInt("KeepAiming", 1, "CAMERA");
    gCfg.FreeLineOfSight      = cfg->GetBool("FreeLineOfSight", true, "CAMERA");
    gCfg.FPCameraCentering    = cfg->GetInt("FPCameraCentering", 1, "CAMERA");
    gCfg.NoAngleLimit         = cfg->GetBool("NoAngleLimit", true, "CAMERA");
    gCfg.RestoreDistance      = clampint(0, 3, cfg->GetInt("RestoreDistance", 0, "CAMERA"));
    gCfg.JumpcutRestore       = clampint(0, 1000, cfg->GetInt("JumpcutRestore", 0, "CAMERA"));
    gCfg.JumpcutCarSpeed      = cfg->GetFloat("JumpcutCarSpeed", 50.0f, "CAMERA");

    gCfg.DisableOnMission     = cfg->GetBool("DisableOnMission", false, "EXCEPTIONS");
    gCfg.FixShootRateOfNPC    = cfg->GetBool("FixShootRateOfNPC", true, "WEAPONSETTINGS");

    // Touch Controls Section
    gCfg.TouchBtnX            = cfg->GetFloat("TouchIconX", 0.82f, "TOUCH");
    gCfg.TouchBtnY            = cfg->GetFloat("TouchIconY", 0.42f, "TOUCH");
    gCfg.TouchBtnSize         = cfg->GetFloat("TouchIconSize", 0.085f, "TOUCH");

    char buf[128];
    cfg->GetString(buf, sizeof(buf), "TweakFOV", "70.0 / 70.0 / 70.0", "CAMERA");
    sscanf(buf, "%f / %f / %f", &gCfg.FOV[0], &gCfg.FOV[1], &gCfg.FOV[2]);

    cfg->GetString(buf, sizeof(buf), "Default", "1.0 / 1.0 / 1.0", "WEAPONSETTINGS");
    sscanf(buf, "%f / %f / %f", &gCfg.DefaultShootRates[0], &gCfg.DefaultShootRates[1], &gCfg.DefaultShootRates[2]);

    for (int i = 0; i < 256; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "%d", i);
        cfg->GetString(buf, sizeof(buf), key, "-2.0 / -2.0 / -2.0", "WEAPONSETTINGS");
        float p = -2.0f, g = -2.0f, h = -2.0f;
        if (sscanf(buf, "%f / %f / %f", &p, &g, &h) == 3) {
            gCfg.WeaponShootRates[i][0] = (p > 0.0f) ? clampfloat(0.01f, 9.99f, p) : p;
            gCfg.WeaponShootRates[i][1] = (g > 0.0f) ? clampfloat(0.01f, 9.99f, g) : g;
            gCfg.WeaponShootRates[i][2] = (h > 0.0f) ? clampfloat(0.01f, 9.99f, h) : h;
        } else {
            gCfg.WeaponShootRates[i][0] = gCfg.WeaponShootRates[i][1] = gCfg.WeaponShootRates[i][2] = -2.0f;
        }
    }

    const char* vehKeys[6] = { "Cars", "Bikes", "Helis", "Planes", "Boats", "Trains" };
    for (int i = 0; i < 6; ++i) {
        cfg->GetString(buf, sizeof(buf), vehKeys[i], "1/1/1", "EXCEPTIONS");
        int b1 = 0, b2 = 0, b3 = 0;
        sscanf(buf, "%d/%d/%d", &b1, &b2, &b3);
        uint8_t mask = 0;
        if (b1 > 0) mask |= (1 << 1);
        if (b2 > 0) mask |= (1 << 2);
        if (b3 > 0) mask |= (1 << 3);
        gCfg.VehicleSubclassFlags[i] = mask;
    }

    logger->Info("DrivebySettings.ini loaded successfully.");
}

// ============================================================================
// Weapon & Shoot Rate Calculation (Ported from MANDRBY :GetShootRate)
// ============================================================================
inline uint8_t GetPedActiveWeaponSlot(uintptr_t ped) {
    return *(uint8_t*)(ped + 0x8B8);
}

inline CWeapon* GetPedWeaponSlotPtr(uintptr_t ped, int slot) {
    return (CWeapon*)(ped + 0x710 + (slot * sizeof(CWeapon)));
}

float CalculateWeaponShootRate(uintptr_t ped, int weaponType) {
    if (!GetWeaponInfo) return 1.0f;
    int8_t skill = GetWeaponSkill ? GetWeaponSkill(ped) : 1;
    skill = (int8_t)clampint(0, 2, (int)skill);

    CWeaponInfo* wInfo = GetWeaponInfo(weaponType, skill);
    if (!wInfo) return 1.0f;

    float mult = -2.0f;
    if (weaponType >= 0 && weaponType < 256) {
        mult = gCfg.WeaponShootRates[weaponType][skill];
    }
    if (mult == -2.0f) {
        mult = gCfg.DefaultShootRates[skill];
    }
    if (mult <= 0.0f) return mult; // 0.0 or negative disables weapon in driveby

    mult = clampfloat(0.01f, 9.99f, mult);

    // Dual-wield compensation (0x3833) from MANDRBY/DBYFNPC
    bool isDual = (wInfo->m_nFlags == 0x3833);
    if (isDual && skill > 0) {
        CWeaponInfo* prevInfo = GetWeaponInfo(weaponType, skill - 1);
        if (prevInfo) wInfo = prevInfo;
    }

    float loopDiff = wInfo->m_fAnimLoop2End - wInfo->m_fAnimLoop2Start;
    if (loopDiff > 0.001f) {
        float factor = std::pow(loopDiff, -0.9596f) * 0.1749f;
        if (isDual) factor *= 1.06f;
        mult *= factor;
    }

    return clampfloat(0.01f, 8.0f, mult);
}

void ApplyAnimShootRate(int actorHandle, float speed) {
    for (int i = 0; i < 16; ++i) {
        if (ScriptCommand(&scm_is_char_playing_anim, actorHandle, kDrivebyAnims[i])) {
            ScriptCommand(&scm_set_char_anim_speed, actorHandle, kDrivebyAnims[i], speed);
            return;
        }
    }
}

void CyclePlayerWeapon(uintptr_t playerPed, int dir) {
    int slot = GetPedActiveWeaponSlot(playerPed);
    for (int i = 0; i < 13; ++i) {
        slot += dir;
        if (slot < 0) slot = 12;
        if (slot > 12) slot = 0;

        CWeapon* weap = GetPedWeaponSlotPtr(playerPed, slot);
        if (slot == 0 || weap->m_nTotalAmmo > 0) {
            float rate = (slot == 0) ? 1.0f : CalculateWeaponShootRate(playerPed, weap->m_eWeaponType);
            if (rate > 0.0f) {
                if (SetCurrentWeapon) SetCurrentWeapon(playerPed, weap->m_eWeaponType);
                gState.activeWeaponSlot = slot;
                gState.activeWeaponType = weap->m_eWeaponType;
                gState.currentShootRate = rate;
                break;
            }
        }
    }
}

// ============================================================================
// Special Weapons: Spray (Flamethrower 37, Spraycan 41, Extinguisher 42)
// & ThrowExplosives (Grenade 16, Teargas 17, Molotov 18, Satchel 39, Detonator 40)
// ============================================================================
void ProcessSpecialWeapons(uintptr_t playerPed, uintptr_t vehicle, bool firePressed) {
    if (!GetPedRef || !GetVehicleRef) return;
    int playerHandle = GetPedRef(playerPed);
    int vehHandle = GetVehicleRef(vehicle);

    int slot = GetPedActiveWeaponSlot(playerPed);
    CWeapon* weap = GetPedWeaponSlotPtr(playerPed, slot);
    int weapType = weap->m_eWeaponType;

    // 1. Detonator (Weapon 40)
    if (weapType == 40 && firePressed) {
        ScriptCommand(&scm_use_detonator);
        weap->m_nTotalAmmo = 0;
        CyclePlayerWeapon(playerPed, -1);
        return;
    }

    // 2. Area Effect Spray Weapons (37: Flamethrower, 41: Spraycan, 42: Extinguisher)
    if (weapType == 37 || weapType == 41 || weapType == 42) {
        if (firePressed && weap->m_nTotalAmmo > 0) {
            if (!gState.isSpecialActionActive) {
                gState.isSpecialActionActive = true;
                ScriptCommand(&scm_task_play_anim, playerHandle, "DRIVEBY_L", "PED", 4.0f, 0, 0, 0, 1, -1);
            }
            if (FireAreaEffect) {
                CVector handPos{0, 0, 0};
                if (GetBonePosition) GetBonePosition(playerPed, handPos, 25, true);
                CVector targetPos = { handPos.x + 2.0f, handPos.y + 2.0f, handPos.z };
                FireAreaEffect(weap, playerPed, &handPos, 0, &targetPos);
                if (weap->m_nTotalAmmo > 0) weap->m_nTotalAmmo--;
            }
        } else if (gState.isSpecialActionActive && !firePressed) {
            gState.isSpecialActionActive = false;
            ScriptCommand(&scm_clear_char_tasks, playerHandle);
        }
        return;
    }

    // 3. Projectiles (16: Grenade, 17: Teargas, 18: Molotov, 39: Satchel)
    if (weapType == 16 || weapType == 17 || weapType == 18 || weapType == 39) {
        uint32_t now = m_snTimeInMilliseconds ? *m_snTimeInMilliseconds : 0;
        if (firePressed && weap->m_nTotalAmmo > 0) {
            if (!gState.isHoldingThrow) {
                gState.isHoldingThrow = true;
                gState.holdStartTime = now;
                if (gCfg.PrepareThrowAnim) {
                    ScriptCommand(&scm_task_play_anim, playerHandle, "CAR_TUNE_RADIO", "PED", 4.0f, 0, 0, 0, 0, -1);
                }
            }
        } else if (gState.isHoldingThrow && !firePressed) {
            gState.isHoldingThrow = false;
            float holdDuration = (float)(now - gState.holdStartTime) / 714.3f;
            float force = clampfloat(0.15f, 2.5f, holdDuration);

            ScriptCommand(&scm_task_play_anim, playerHandle, "DRIVEBY_L", "PED", 4.0f, 0, 0, 0, 1, -1);

            if (AddProjectile && GetBonePosition && Find3rdPersonCamTargetVector) {
                CVector spawnPos{0, 0, 0}, camPos{0, 0, 0}, aimTarget{0, 0, 0};
                GetBonePosition(playerPed, spawnPos, 25, true);
                spawnPos.z += 0.35f;
                Find3rdPersonCamTargetVector(TheCamera, 20.0f, spawnPos, camPos, aimTarget);
                CVector dir = { aimTarget.x - spawnPos.x, aimTarget.y - spawnPos.y, 0.15f };
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }

                AddProjectile(playerPed, weapType, spawnPos, force, &dir, 0);
                if (weap->m_nTotalAmmo > 0) weap->m_nTotalAmmo--;
            }
        }
    }
}

// ============================================================================
// Toggle Manual Driveby Mode (Touch & Controller)
// ============================================================================
void SetManualDrivebyMode(uintptr_t playerPed, uintptr_t vehicle, bool enable) {
    if (!GetPedRef || !GetVehicleRef) return;
    int playerHandle = GetPedRef(playerPed);
    int vehHandle = GetVehicleRef(vehicle);

    if (enable && !gState.isAimingDriveby) {
        int slot = GetPedActiveWeaponSlot(playerPed);
        CWeapon* weap = GetPedWeaponSlotPtr(playerPed, slot);
        float rate = CalculateWeaponShootRate(playerPed, weap->m_eWeaponType);

        if (slot == 0 || weap->m_nTotalAmmo == 0 || rate <= 0.0f) {
            if (ReportFrontendAudioEvent && pAudioEngine) {
                ReportFrontendAudioEvent(pAudioEngine, 41, 1.0f, 0.0f); // Empty Click Sound
            }
            return;
        }

        gState.isAimingDriveby = true;
        gState.currentShootRate = rate;

        // Preload custom IFP if configured
        if (gCfg.PreLoadIFP && !gState.ifpLoaded) {
            if (!ScriptCommand(&scm_has_animation_loaded, "DRIVEBYS")) {
                ScriptCommand(&scm_request_animation, "DRIVEBYS");
            } else {
                gState.ifpLoaded = true;
            }
        }

        if (gCfg.DisableBikeWeakPoint) {
            ScriptCommand(&scm_set_petrol_tank_weakpoint, vehHandle, 0);
        }

        // Apply TweakFOV
        SCRIPT_VAR fovVar;
        ScriptCommand(&scm_get_camera_fov, &fovVar);
        gState.savedFov = fovVar.fParam;
        if (std::fabs(gCfg.FOV[1] - gState.savedFov) > 0.5f) {
            ScriptCommand(&scm_camera_set_lerp_fov, gState.savedFov, gCfg.FOV[1], 400, 0);
            ScriptCommand(&scm_camera_persist_fov, 1);
        }

        // Start 0713: task_drive_by
        ScriptCommand(&scm_task_drive_by, playerHandle, -1, -1, 0.0f, 0.0f, 0.0f, 600.0f, 4, 0, 100);
    } else if (!enable && gState.isAimingDriveby) {
        gState.isAimingDriveby = false;

        ScriptCommand(&scm_camera_persist_fov, 0);
        ScriptCommand(&scm_camera_set_lerp_fov, gCfg.FOV[1], gState.savedFov, 200, 0);

        if (gCfg.DisableBikeWeakPoint && vehicle) {
            ScriptCommand(&scm_set_petrol_tank_weakpoint, vehHandle, 1);
        }

        ScriptCommand(&scm_clear_char_tasks, playerHandle);
        if (gCfg.JumpcutRestore > 0) {
            ScriptCommand(&scm_restore_camera_jumpcut);
        }
    }
}

// ============================================================================
// DBYFNPC: Ped Reactions & Grove Street Recruitment
// ============================================================================
void ProcessNPCFixesAndReactions(uintptr_t playerPed, uintptr_t vehicle) {
    if (!GetBonePosition || !Find3rdPersonCamTargetVector || !GetPedRef) return;

    // Clean up static 3D recruitment marker every frame
    if (gState.recruitMarkerHandle != -1) {
        ScriptCommand(&scm_remove_user_3d_marker, gState.recruitMarkerHandle);
        gState.recruitMarkerHandle = -1;
    }

    if (!gState.isAimingDriveby || gCfg.FixPedReactions <= 0) return;

    int vehHandle = GetVehicleRef(vehicle);
    SCRIPT_VAR speedVar;
    ScriptCommand(&scm_get_car_speed, vehHandle, &speedVar);
    if (speedVar.fParam > 20.0f) return;

    if (pIgnoreEntity) *pIgnoreEntity = (void*)vehicle;

    CVector handPos{0, 0, 0}, camOut{0, 0, 0}, targetOut{0, 0, 0};
    GetBonePosition(playerPed, handPos, 25, true);
    Find3rdPersonCamTargetVector(TheCamera, 25.0f, handPos, camOut, targetOut);

    if (pIgnoreEntity) *pIgnoreEntity = nullptr;
}

// ============================================================================
// Touchscreen UI & Input Hooks
// ============================================================================
CRect GetDrivebyTouchRect() {
    int scrW = 1920, scrH = 1080;
    aml->GetDisplaySize(&scrW, &scrH);

    float btnDim = (float)scrH * gCfg.TouchBtnSize;
    float cx = (float)scrW * gCfg.TouchBtnX;
    float cy = (float)scrH * gCfg.TouchBtnY;

    return CRect{
        cx - btnDim * 0.5f,
        cy + btnDim * 0.5f,
        cx + btnDim * 0.5f,
        cy - btnDim * 0.5f
    };
}

DECL_HOOKv(CHud_Draw) {
    CHud_Draw();

    uintptr_t playerPed = FindPlayerPed ? FindPlayerPed(-1) : 0;
    uintptr_t vehicle   = FindPlayerVehicle ? FindPlayerVehicle(-1, false) : 0;

    if (!playerPed || !vehicle) {
        gState.isAimingDriveby = false;
        return;
    }

    if (!gState.toggleIconSprite.m_pTexture && GetTextureFromDB) {
        gState.toggleIconSprite.m_pTexture = GetTextureFromDB("hud_driveby");
    }

    if (gState.toggleIconSprite.m_pTexture && DrawSprite) {
        CRect rect = GetDrivebyTouchRect();
        CRGBA tint = gState.isAimingDriveby ? CRGBA{140, 255, 140, 245} : CRGBA{255, 255, 255, 175};
        DrawSprite(&gState.toggleIconSprite, rect, tint);
    }
}

DECL_HOOKv(AND_TouchEvent, int type, int fingerId, int x, int y) {
    uintptr_t playerPed = FindPlayerPed ? FindPlayerPed(-1) : 0;
    uintptr_t vehicle   = FindPlayerVehicle ? FindPlayerVehicle(-1, false) : 0;

    if (playerPed && vehicle && type == 1) { // Touch Down
        CRect rect = GetDrivebyTouchRect();
        if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) {
            aml->DoVibro(20); // Subtle 20ms tactile feedback on toggle
            SetManualDrivebyMode(playerPed, vehicle, !gState.isAimingDriveby);
            return;
        }
    }
    AND_TouchEvent(type, fingerId, x, y);
}

// ============================================================================
// Main Frame Loop & Controller Hooks
// ============================================================================
DECL_HOOKv(CGame_Process) {
    CGame_Process();

    uintptr_t playerPed = FindPlayerPed ? FindPlayerPed(-1) : 0;
    if (!playerPed) return;

    uintptr_t vehicle = FindPlayerVehicle ? FindPlayerVehicle(-1, false) : 0;
    if (!vehicle) {
        if (gState.isAimingDriveby) {
            gState.isAimingDriveby = false;
            ScriptCommand(&scm_camera_persist_fov, 0);
        }
        gState.lastVehicle = 0;
        return;
    }

    int playerHandle = GetPedRef ? GetPedRef(playerPed) : -1;

    // Apply custom shoot rate speed to active driveby animation
    if (gState.isAimingDriveby && playerHandle != -1) {
        ApplyAnimShootRate(playerHandle, gState.currentShootRate);
    }

    // Controller Input Handling
    bool firePressed = false;
    if (ThePads) {
        CPad& pad = ThePads[0];

        int16_t aimNew = pad.NewState.LeftShoulder1;
        int16_t aimOld = pad.OldState.LeftShoulder1;
        int16_t shootNew = pad.NewState.ButtonCircle;

        if (gCfg.DrivebyControlType == 3) {
            aimNew = pad.NewState.LeftShoulder2;
            aimOld = pad.OldState.LeftShoulder2;
            shootNew = pad.NewState.RightShoulder2;
        } else if (gCfg.DrivebyControlType == 4) {
            aimNew = pad.NewState.RightShoulder2;
            aimOld = pad.OldState.RightShoulder2;
            shootNew = pad.NewState.LeftShoulder2;
        } else if (gCfg.DrivebyControlType == 5) {
            shootNew = pad.NewState.RightShoulder1;
        } else if (gCfg.DrivebyControlType == 6) {
            aimNew = pad.NewState.RightShoulder1;
            aimOld = pad.OldState.RightShoulder1;
            shootNew = pad.NewState.LeftShoulder1;
        }

        firePressed = (shootNew != 0);

        // Aim button toggle on controller
        if (aimNew != 0 && aimOld == 0) {
            SetManualDrivebyMode(playerPed, vehicle, !gState.isAimingDriveby);
        }

        // D-Pad Weapon Switching in Vehicle
        if (gCfg.DPadButtonsMode > 0) {
            bool nextW = (gCfg.DPadButtonsMode > 1)
                ? (pad.NewState.DPadUp != 0 && pad.OldState.DPadUp == 0)
                : (pad.NewState.DPadRight != 0 && pad.OldState.DPadRight == 0);
            bool prevW = (gCfg.DPadButtonsMode > 1)
                ? (pad.NewState.DPadDown != 0 && pad.OldState.DPadDown == 0)
                : (pad.NewState.DPadLeft != 0 && pad.OldState.DPadLeft == 0);

            if (nextW) {
                CyclePlayerWeapon(playerPed, 1);
                pad.NewState.DPadUp = pad.NewState.DPadRight = 0;
            } else if (prevW) {
                CyclePlayerWeapon(playerPed, -1);
                pad.NewState.DPadDown = pad.NewState.DPadLeft = 0;
            }
        }

        // Emulate fire button in CTaskSimpleGangDriveBy when shooting
        if (gState.isAimingDriveby && firePressed) {
            pad.NewState.ButtonCircle = 0xFF;
            if (m_snTimeInMilliseconds) gState.lastPlayerShotTime = *m_snTimeInMilliseconds;
        }

        // Enforce NoDrivingWhileAiming
        if (gState.isAimingDriveby && gCfg.NoDrivingWhileAiming > 0) {
            pad.NewState.LeftStickX = 0;
            pad.NewState.ButtonCross = 0;
            pad.NewState.ButtonSquare = 0;
        }
    }

    // Handle Spray & Projectiles
    ProcessSpecialWeapons(playerPed, vehicle, firePressed);

    // Handle NPC Reactions & Recruitment (DBYFNPC)
    ProcessNPCFixesAndReactions(playerPed, vehicle);
}

DECL_HOOKb(CPad_GetLookLeft, CPad* self, bool b1) {
    if (gState.isAimingDriveby) return false;
    return CPad_GetLookLeft(self, b1);
}

DECL_HOOKb(CPad_GetLookRight, CPad* self, bool b1) {
    if (gState.isAimingDriveby) return false;
    return CPad_GetLookRight(self, b1);
}

DECL_HOOKb(CPad_GetLookBehindForCar, CPad* self) {
    if (gState.isAimingDriveby) return false;
    return CPad_GetLookBehindForCar(self);
}

// ============================================================================
// AML 1.4.0 Lifecycle Callbacks
// ============================================================================
ON_MOD_PRELOAD() {
    logger->SetTag("ManualDriveby64");
}

ON_MOD_LOAD() {
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");
    if (!pGTASA || !hGTASA) {
        logger->Error("libGTASA.so not found! Aborting.");
        return;
    }

    LoadSettingsFromINI();

    // Resolve SCM Engine Symbols
    SETSYM_TO(ProcessOneCommand,            hGTASA, "_ZN14CRunningScript17ProcessOneCommandEv");
    SETSYM_TO(ScriptParams,                 hGTASA, "ScriptParams");

    // Resolve Core Engine Globals & Functions
    SETSYM_TO(ThePads,                      hGTASA, "_ZN4CPad4PadsE");
    SETSYM_TO(TheCamera,                    hGTASA, "TheCamera");
    SETSYM_TO(pIgnoreEntity,                hGTASA, "_ZN6CWorld13pIgnoreEntityE");
    SETSYM_TO(m_snTimeInMilliseconds,       hGTASA, "_ZN6CTimer22m_snTimeInMillisecondsE");
    SETSYM_TO(pAudioEngine,                 hGTASA, "AudioEngine");

    SETSYM_TO(FindPlayerPed,                hGTASA, "_Z13FindPlayerPedi");
    SETSYM_TO(FindPlayerVehicle,            hGTASA, "_Z17FindPlayerVehicleib");
    SETSYM_TO(GetPedRef,                    hGTASA, "_ZN6CPools9GetPedRefEP4CPed");
    SETSYM_TO(GetVehicleRef,                hGTASA, "_ZN6CPools13GetVehicleRefEP8CVehicle");
    SETSYM_TO(GetPedFromRef,                hGTASA, "_ZN6CPools6GetPedEi");
    SETSYM_TO(GetBonePosition,              hGTASA, "_ZN4CPed15GetBonePositionER7RwV3djb");
    SETSYM_TO(Find3rdPersonCamTargetVector, hGTASA, "_ZN7CCamera28Find3rdPersonCamTargetVectorEf7CVectorRS0_S1_");
    SETSYM_TO(SetCurrentWeapon,             hGTASA, "_ZN4CPed16SetCurrentWeaponE11eWeaponType");
    SETSYM_TO(GetWeaponInfo,                hGTASA, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea");
    SETSYM_TO(GetWeaponSkill,               hGTASA, "_ZN4CPed14GetWeaponSkillEv");
    SETSYM_TO(FireAreaEffect,               hGTASA, "_ZN7CWeapon14FireAreaEffectEP7CEntityP7CVectorS1_S3_");
    SETSYM_TO(AddProjectile,                hGTASA, "_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorfPS3_S1_");
    SETSYM_TO(MakeThisPedJoinOurGroup,      hGTASA, "_ZN10CPlayerPed23MakeThisPedJoinOurGroupEP4CPed");
    SETSYM_TO(StartOffensiveGangWar,        hGTASA, "_ZN9CGangWars21StartOffensiveGangWarEv");
    SETSYM_TO(GetTextureFromDB,             hGTASA, "_ZN22TextureDatabaseRuntime10GetTextureEPKc");
    SETSYM_TO(DrawSprite,                   hGTASA, "_ZN9CSprite2d4DrawERK5CRectRK5CRGBA");
    SETSYM_TO(ReportFrontendAudioEvent,     hGTASA, "_ZN12CAudioEngine24ReportFrontendAudioEventEiff");

    // Install Hooks via HOOKSYM
    HOOKSYM(CHud_Draw,                 hGTASA, "_ZN4CHud4DrawEv");
    HOOKSYM(AND_TouchEvent,            hGTASA, "_Z14AND_TouchEventiiii");
    HOOKSYM(CGame_Process,             hGTASA, "_ZN5CGame7ProcessEv");
    HOOKSYM(CPad_GetLookLeft,          hGTASA, "_ZN4CPad11GetLookLeftEb");
    HOOKSYM(CPad_GetLookRight,         hGTASA, "_ZN4CPad12GetLookRightEb");
    HOOKSYM(CPad_GetLookBehindForCar,  hGTASA, "_ZN4CPad20GetLookBehindForCarEv");

    logger->Info("Manual Driveby Refixed 64-bit loaded successfully.");
}
