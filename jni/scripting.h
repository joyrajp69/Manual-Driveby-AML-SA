#pragma once
#include <cstdint>
#include <cstdarg>
#include <cstring>

#define MAX_SCRIPT_VARS 16
#define MAX_SCRIPT_SIZE 255

struct SCRIPT_COMMAND {
    uint16_t OpCode;
    char Params[MAX_SCRIPT_VARS];
};

union SCRIPT_VAR {
    uint32_t dwParam;
    int32_t  iParam;
    float    fParam;
    void*    pParam;
    char*    szParam;
};

// 64-bit CRunningScript layout for GTA SA Android v2.10/v2.11 (arm64-v8a)
#pragma pack(push, 1)
struct CRunningScript {
    CRunningScript* Next;                // 0x00
    CRunningScript* Prev;                // 0x08
    char            Name[8];             // 0x10
    uint8_t*        BaseIP;              // 0x18
    uint8_t*        CurrentIP;           // 0x20
    uint8_t*        Stack[8];            // 0x28
    uint16_t        SP;                  // 0x68
    uint8_t         pad6A[2];            // 0x6A
    SCRIPT_VAR      LocalVar[32];        // 0x6C
    int32_t         Timers[2];           // 0xEC
    bool            bIsActive;           // 0xF4
    bool            bCondResult;         // 0xF5
    bool            bUseMissionCleanup;  // 0xF6
    bool            bIsExternal;         // 0xF7
    bool            bTextBlockOverride;  // 0xF8
    uint8_t         bsd;                 // 0xF9
    uint8_t         padFA[2];            // 0xFA
    int32_t         ActivationTime;      // 0xFC
    uint16_t        LogicalOp;           // 0x100
    bool            NotFlag;             // 0x102
    bool            bWastedBustedCheck;  // 0x103
    bool            bWastedOrBusted;     // 0x104
    uint8_t         pad105[3];           // 0x105
    uintptr_t       SceneSkipIP;         // 0x108
    bool            bIsMission;          // 0x110
    uint8_t         pad111[7];           // 0x111
};
#pragma pack(pop)

extern int8_t (*ProcessOneCommand)(CRunningScript* pScript);
extern SCRIPT_VAR* ScriptParams; // Global `ScriptParams` array in libGTASA.so

inline int ScriptCommand(const SCRIPT_COMMAND* pScmCmd, ...) {
    if (!ProcessOneCommand || !ScriptParams) return 0;

    va_list ap;
    const char* p = pScmCmd->Params;
    va_start(ap, pScmCmd);

    uint8_t cmdBuf[MAX_SCRIPT_SIZE];
    memset(cmdBuf, 0, sizeof(cmdBuf));

    CRunningScript script;
    memset(&script, 0, sizeof(script));
    strcpy(script.Name, "aml-drb");
    script.bIsActive = true;
    script.bIsExternal = true;
    script.BaseIP = script.CurrentIP = cmdBuf;

    uint16_t op = pScmCmd->OpCode;
    bool notFlag = (op >> 15) != 0;
    op &= 0x7FFF;

    cmdBuf[0] = op & 0xFF;
    cmdBuf[1] = (op >> 8) & 0xFF;
    int bufIdx = 2;
    int varIdx = 0;
    SCRIPT_VAR* savedVars[MAX_SCRIPT_VARS] = { nullptr };

    while (*p) {
        switch (*p) {
            case 'i': // Integer (32-bit)
            case 'd': {
                int32_t v = va_arg(ap, int32_t);
                cmdBuf[bufIdx++] = 0x01; // DataType: Int32
                memcpy(&cmdBuf[bufIdx], &v, 4);
                bufIdx += 4;
                break;
            }
            case 'f': { // Float
                float v = (float)va_arg(ap, double);
                cmdBuf[bufIdx++] = 0x06; // DataType: Float
                memcpy(&cmdBuf[bufIdx], &v, 4);
                bufIdx += 4;
                break;
            }
            case 'v': { // Output Variable Reference (writes back via ScriptParams)
                SCRIPT_VAR* outPtr = va_arg(ap, SCRIPT_VAR*);
                savedVars[varIdx] = outPtr;
                ScriptParams[varIdx].dwParam = outPtr->dwParam;
                cmdBuf[bufIdx++] = 0x03; // Local var
                uint16_t vOff = varIdx;
                script.LocalVar[varIdx].dwParam = outPtr->dwParam;
                memcpy(&cmdBuf[bufIdx], &vOff, 2);
                bufIdx += 2;
                varIdx++;
                break;
            }
            case 's': { // Short string / GXT label (8 bytes)
                const char* sz = va_arg(ap, const char*);
                cmdBuf[bufIdx++] = 0x09; // String8
                strncpy((char*)&cmdBuf[bufIdx], sz, 8);
                bufIdx += 8;
                break;
            }
            case 'z': { // Long string (16 bytes)
                const char* sz = va_arg(ap, const char*);
                cmdBuf[bufIdx++] = 0x0E; // Variable length string
                uint8_t len = (uint8_t)strlen(sz);
                cmdBuf[bufIdx++] = len;
                memcpy(&cmdBuf[bufIdx], sz, len);
                bufIdx += len;
                break;
            }
        }
        ++p;
    }
    va_end(ap);

    ProcessOneCommand(&script);

    for (int i = 0; i < varIdx; ++i) {
        if (savedVars[i]) {
            savedVars[i]->dwParam = script.LocalVar[i].dwParam;
        }
    }

    return (notFlag ? !script.bCondResult : script.bCondResult) ? 1 : 0;
}

// ============================================================================
// Complete SCM Opcode Definitions Used in ManualDrivebyRefixed & DBYFNPC
// ============================================================================
const SCRIPT_COMMAND scm_request_animation           = { 0x04ED, "z" };
const SCRIPT_COMMAND scm_has_animation_loaded        = { 0x04EE, "z" };
const SCRIPT_COMMAND scm_remove_animation            = { 0x04EF, "z" };
const SCRIPT_COMMAND scm_request_model               = { 0x0247, "i" };
const SCRIPT_COMMAND scm_has_model_loaded            = { 0x0248, "i" };
const SCRIPT_COMMAND scm_load_all_models_now         = { 0x038B, "" };
const SCRIPT_COMMAND scm_mark_model_as_no_longer_needed = { 0x0249, "i" };

// Tasks & Driveby
const SCRIPT_COMMAND scm_task_drive_by               = { 0x0713, "iiiffffiii" };
const SCRIPT_COMMAND scm_task_play_anim              = { 0x0605, "izzfiiiii" };
const SCRIPT_COMMAND scm_task_pick_up_object         = { 0x070A, "iifffiizzi" };
const SCRIPT_COMMAND scm_clear_char_tasks            = { 0x0687, "i" };
const SCRIPT_COMMAND scm_is_char_playing_anim        = { 0x0611, "iz" };
const SCRIPT_COMMAND scm_get_char_anim_current_time  = { 0x0613, "izv" };
const SCRIPT_COMMAND scm_set_char_anim_speed         = { 0x0393, "izf" };
const SCRIPT_COMMAND scm_hide_char_weapon_cutscene   = { 0x06AB, "ii" };

// Objects & FX Systems (Spray / Flamethrower / Extinguisher / ThrowExplosives)
const SCRIPT_COMMAND scm_create_object               = { 0x0107, "ifffv" };
const SCRIPT_COMMAND scm_delete_object               = { 0x0108, "i" };
const SCRIPT_COMMAND scm_does_object_exist           = { 0x03CA, "i" };
const SCRIPT_COMMAND scm_set_object_collision        = { 0x0382, "ii" };
const SCRIPT_COMMAND scm_set_object_records_col      = { 0x04D9, "ii" };
const SCRIPT_COMMAND scm_set_object_heading          = { 0x0177, "if" };
const SCRIPT_COMMAND scm_set_object_coordinates      = { 0x01BC, "ifff" };
const SCRIPT_COMMAND scm_set_object_visible          = { 0x0750, "ii" };
const SCRIPT_COMMAND scm_sort_out_obj_col_with_car   = { 0x050E, "ii" };
const SCRIPT_COMMAND scm_create_fx_on_obj_with_dir   = { 0x066E, "ziiffffffiv" };
const SCRIPT_COMMAND scm_play_fx_system              = { 0x064C, "i" };
const SCRIPT_COMMAND scm_kill_fx_system              = { 0x0650, "i" };
const SCRIPT_COMMAND scm_use_detonator               = { 0x09D9, "" };

// Vehicles, Camera & Doors
const SCRIPT_COMMAND scm_get_car_speed               = { 0x02E3, "iv" };
const SCRIPT_COMMAND scm_get_car_heading             = { 0x0174, "iv" };
const SCRIPT_COMMAND scm_set_car_visible             = { 0x0338, "ii" };
const SCRIPT_COMMAND scm_set_petrol_tank_weakpoint   = { 0x09C4, "ii" };
const SCRIPT_COMMAND scm_damage_car_panel            = { 0x0730, "ii" };
const SCRIPT_COMMAND scm_pop_car_panel               = { 0x0697, "iii" };
const SCRIPT_COMMAND scm_restore_camera_jumpcut      = { 0x02EB, "" };
const SCRIPT_COMMAND scm_camera_set_lerp_fov         = { 0x0922, "ffii" };
const SCRIPT_COMMAND scm_camera_persist_fov          = { 0x0931, "i" };
const SCRIPT_COMMAND scm_get_camera_fov              = { 0x0801, "v" };
const SCRIPT_COMMAND scm_set_photo_camera_effect     = { 0x0A2F, "i" };
const SCRIPT_COMMAND scm_take_photo                  = { 0x0A1E, "i" };
const SCRIPT_COMMAND scm_is_line_of_sight_clear      = { 0x06BD, "ffffffiiiii" };
const SCRIPT_COMMAND scm_get_offset_from_car         = { 0x0407, "ifffvvv" };
const SCRIPT_COMMAND scm_get_offset_from_object      = { 0x0400, "ifffvvv" };

// NPC Reactions, Sequences & Recruitment (DBYFNPC)
const SCRIPT_COMMAND scm_open_sequence_task          = { 0x0615, "v" };
const SCRIPT_COMMAND scm_close_sequence_task         = { 0x0616, "i" };
const SCRIPT_COMMAND scm_perform_sequence_task       = { 0x0618, "ii" };
const SCRIPT_COMMAND scm_clear_sequence_task         = { 0x061B, "v" };
const SCRIPT_COMMAND scm_task_dive_and_get_up        = { 0x0673, "iffi" };
const SCRIPT_COMMAND scm_task_toggle_duck            = { 0x04EB, "ii" };
const SCRIPT_COMMAND scm_task_weapon_roll            = { 0x06E3, "ii" };
const SCRIPT_COMMAND scm_task_kill_char_on_foot      = { 0x05E2, "ii" };
const SCRIPT_COMMAND scm_task_smart_flee_char        = { 0x05DD, "iifi" };
const SCRIPT_COMMAND scm_task_duck                   = { 0x05C5, "ii" };
const SCRIPT_COMMAND scm_task_hands_up               = { 0x05C4, "ii" };
const SCRIPT_COMMAND scm_task_cower                  = { 0x05C3, "i" };
const SCRIPT_COMMAND scm_task_shake_fist             = { 0x05C2, "i" };
const SCRIPT_COMMAND scm_task_wander_standard        = { 0x05DE, "i" };
const SCRIPT_COMMAND scm_task_look_at_char           = { 0x05BF, "iii" };
const SCRIPT_COMMAND scm_task_turn_char_to_face_char = { 0x0639, "ii" };
const SCRIPT_COMMAND scm_task_enter_car_as_passenger = { 0x05CA, "iiii" };
const SCRIPT_COMMAND scm_task_car_mission            = { 0x06E1, "iiiifii" };
const SCRIPT_COMMAND scm_task_say                    = { 0x05C1, "ii" };
const SCRIPT_COMMAND scm_set_char_shoot_rate         = { 0x07DD, "ii" };
const SCRIPT_COMMAND scm_set_char_stay_in_same_place = { 0x0350, "ii" };
const SCRIPT_COMMAND scm_create_user_3d_marker       = { 0x0A40, "fffiv" };
const SCRIPT_COMMAND scm_remove_user_3d_marker       = { 0x0A41, "i" };
const SCRIPT_COMMAND scm_alter_wanted_level          = { 0x010D, "ii" };
