#include "x64_dbg.h"
#include "debugger_commands.h"
#include "console.h"
#include "memory.h"
#include "variable.h"

CMDRESULT cbDebugSetBPXOptions(int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Not enough arguments!"));
        return STATUS_ERROR;
    }
    DWORD type = 0;
    const char* strType = 0;
    duint setting_type;
    if(strstr(argv[1], "long"))
    {
        setting_type = 1; //break_int3long
        strType = "TYPE_LONG_INT3";
        type = UE_BREAKPOINT_LONG_INT3;
    }
    else if(strstr(argv[1], "ud2"))
    {
        setting_type = 2; //break_ud2
        strType = "TYPE_UD2";
        type = UE_BREAKPOINT_UD2;
    }
    else if(strstr(argv[1], "short"))
    {
        setting_type = 0; //break_int3short
        strType = "TYPE_INT3";
        type = UE_BREAKPOINT_INT3;
    }
    else
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Invalid type specified!"));
        return STATUS_ERROR;
    }
    SetBPXOptions(type);
    BridgeSettingSetUint("Engine", "BreakpointType", setting_type);
    dprintf(QT_TRANSLATE_NOOP("DBG", "Default breakpoint type set to: %s\n"), strType);
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugSetBPX(int argc, char* argv[]) //bp addr [,name [,type]]
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Not enough arguments!"));
        return STATUS_ERROR;
    }
    char argaddr[deflen] = "";
    strcpy_s(argaddr, argv[1]);
    char argname[deflen] = "";
    if(argc > 2)
        strcpy_s(argname, argv[2]);
    char argtype[deflen] = "";
    bool has_arg2 = argc > 3;
    if(has_arg2)
        strcpy_s(argtype, argv[3]);
    if(!has_arg2 && (scmp(argname, "ss") || scmp(argname, "long") || scmp(argname, "ud2")))
    {
        strcpy_s(argtype, argname);
        *argname = 0;
    }
    _strlwr(argtype);
    duint addr = 0;
    if(!valfromstring(argaddr, &addr))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Invalid addr: \"%s\"\n"), argaddr);
        return STATUS_ERROR;
    }
    int type = 0;
    bool singleshoot = false;
    if(strstr(argtype, "ss"))
    {
        type |= UE_SINGLESHOOT;
        singleshoot = true;
    }
    else
        type |= UE_BREAKPOINT;
    if(strstr(argtype, "long"))
        type |= UE_BREAKPOINT_TYPE_LONG_INT3;
    else if(strstr(argtype, "ud2"))
        type |= UE_BREAKPOINT_TYPE_UD2;
    else if(strstr(argtype, "short"))
        type |= UE_BREAKPOINT_TYPE_INT3;
    short oldbytes;
    const char* bpname = 0;
    if(*argname)
        bpname = argname;
    BREAKPOINT bp;
    if(BpGet(addr, BPNORMAL, bpname, &bp))
    {
        if(!bp.enabled)
            return DbgCmdExecDirect(StringUtils::sprintf("bpe %p", bp.addr).c_str()) ? STATUS_CONTINUE : STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "Breakpoint already set!"));
        return STATUS_CONTINUE;
    }
    if(IsBPXEnabled(addr))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Error setting breakpoint at %p! (IsBPXEnabled)\n"), addr);
        return STATUS_ERROR;
    }
    if(!MemRead(addr, &oldbytes, sizeof(short)))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Error setting breakpoint at %p! (memread)\n"), addr);
        return STATUS_ERROR;
    }
    if(!BpNew(addr, true, singleshoot, oldbytes, BPNORMAL, type, bpname))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Error setting breakpoint at %p! (bpnew)\n"), addr);
        return STATUS_ERROR;
    }
    GuiUpdateAllViews();
    if(!SetBPX(addr, type, (void*)cbUserBreakpoint))
    {
        if(!MemIsValidReadPtr(addr))
            return STATUS_CONTINUE;
        dprintf(QT_TRANSLATE_NOOP("DBG", "Error setting breakpoint at %p! (SetBPX)\n"), addr);
        return STATUS_ERROR;
    }
    dprintf(QT_TRANSLATE_NOOP("DBG", "Breakpoint at %p set!\n"), addr);
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugDeleteBPX(int argc, char* argv[])
{
    if(argc < 2) //delete all breakpoints
    {
        if(!BpGetCount(BPNORMAL))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No breakpoints to delete!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbDeleteAllBreakpoints))  //at least one deletion failed
        {
            GuiUpdateAllViews();
            return STATUS_ERROR;
        }
        dputs(QT_TRANSLATE_NOOP("DBG", "All breakpoints deleted!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    if(BpGet(0, BPNORMAL, argv[1], &found)) //found a breakpoint with name
    {
        if(!BpDelete(found.addr, BPNORMAL))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Delete breakpoint failed (bpdel): %p\n"), found.addr);
            return STATUS_ERROR;
        }
        if(found.enabled && !DeleteBPX(found.addr))
        {
            GuiUpdateAllViews();
            if(!MemIsValidReadPtr(found.addr))
                return STATUS_CONTINUE;
            dprintf(QT_TRANSLATE_NOOP("DBG", "Delete breakpoint failed (DeleteBPX): %p\n"), found.addr);
            return STATUS_ERROR;
        }
        return STATUS_CONTINUE;
    }
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPNORMAL, 0, &found)) //invalid breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!BpDelete(found.addr, BPNORMAL))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Delete breakpoint failed (bpdel): %p\n"), found.addr);
        return STATUS_ERROR;
    }
    if(found.enabled && !DeleteBPX(found.addr))
    {
        GuiUpdateAllViews();
        if(!MemIsValidReadPtr(found.addr))
            return STATUS_CONTINUE;
        dprintf(QT_TRANSLATE_NOOP("DBG", "Delete breakpoint failed (DeleteBPX): %p\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Breakpoint deleted!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugEnableBPX(int argc, char* argv[])
{
    if(argc < 2) //enable all breakpoints
    {
        if(!BpGetCount(BPNORMAL))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No breakpoints to enable!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbEnableAllBreakpoints)) //at least one enable failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All breakpoints enabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    if(BpGet(0, BPNORMAL, argv[1], &found)) //found a breakpoint with name
    {
        if(!SetBPX(found.addr, found.titantype, (void*)cbUserBreakpoint))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable breakpoint %p (SetBPX)\n"), found.addr);
            return STATUS_ERROR;
        }
        if(!BpEnable(found.addr, BPNORMAL, true))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable breakpoint %p (BpEnable)\n"), found.addr);
            return STATUS_ERROR;
        }
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPNORMAL, 0, &found)) //invalid breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(found.enabled)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Breakpoint already enabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    if(!SetBPX(found.addr, found.titantype, (void*)cbUserBreakpoint))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable breakpoint %p (SetBPX)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!BpEnable(found.addr, BPNORMAL, true))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable breakpoint %p (BpEnable)\n"), found.addr);
        return STATUS_ERROR;
    }
    GuiUpdateAllViews();
    dputs(QT_TRANSLATE_NOOP("DBG", "Breakpoint enabled!"));
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugDisableBPX(int argc, char* argv[])
{
    if(argc < 2) //delete all breakpoints
    {
        if(!BpGetCount(BPNORMAL))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No breakpoints to disable!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbDisableAllBreakpoints)) //at least one deletion failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All breakpoints disabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    if(BpGet(0, BPNORMAL, argv[1], &found)) //found a breakpoint with name
    {
        if(!BpEnable(found.addr, BPNORMAL, false))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable breakpoint %p (BpEnable)\n"), found.addr);
            return STATUS_ERROR;
        }
        if(!DeleteBPX(found.addr))
        {
            GuiUpdateAllViews();
            if(!MemIsValidReadPtr(found.addr))
                return STATUS_CONTINUE;
            dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable breakpoint %p (DeleteBPX)\n"), found.addr);
            return STATUS_ERROR;
        }
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPNORMAL, 0, &found)) //invalid breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!found.enabled)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Breakpoint already disabled!"));
        return STATUS_CONTINUE;
    }
    if(!BpEnable(found.addr, BPNORMAL, false))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable breakpoint %p (BpEnable)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!DeleteBPX(found.addr))
    {
        GuiUpdateAllViews();
        if(!MemIsValidReadPtr(found.addr))
            return STATUS_CONTINUE;
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable breakpoint %p (DeleteBPX)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Breakpoint disabled!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

static CMDRESULT cbDebugSetBPXTextCommon(BP_TYPE Type, int argc, char* argv[], const String & description, std::function<bool(duint, BP_TYPE, const char*)> setFunction)
{
    BREAKPOINT bp;
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!\n"));
        return STATUS_ERROR;
    }
    auto value = "";
    if(argc > 2)
        value = argv[2];

    if(!BpGetAny(Type, argv[1], &bp))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!setFunction(bp.addr, Type, value))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Can't set %s on breakpoint \"%s\"\n"), description, argv[1]);
        return STATUS_ERROR;
    }
    DebugUpdateBreakpointsViewAsync();
    return STATUS_CONTINUE;
}

static CMDRESULT cbDebugSetBPXNameCommon(BP_TYPE Type, int argc, char* argv[])
{
    return cbDebugSetBPXTextCommon(Type, argc, argv, String(GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "breakpoint name"))), BpSetName);
}

static CMDRESULT cbDebugSetBPXConditionCommon(BP_TYPE Type, int argc, char* argv[])
{
    return cbDebugSetBPXTextCommon(Type, argc, argv, String(GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "break condition"))), BpSetBreakCondition);
}

static CMDRESULT cbDebugSetBPXLogCommon(BP_TYPE Type, int argc, char* argv[])
{
    return cbDebugSetBPXTextCommon(Type, argc, argv, String(GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "logging text"))), BpSetLogText);
}

static CMDRESULT cbDebugSetBPXLogConditionCommon(BP_TYPE Type, int argc, char* argv[])
{
    return cbDebugSetBPXTextCommon(Type, argc, argv, String(GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "logging condition"))), BpSetLogCondition);
}

static CMDRESULT cbDebugSetBPXCommandCommon(BP_TYPE Type, int argc, char* argv[])
{
    return cbDebugSetBPXTextCommon(Type, argc, argv, String(GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "command on hit"))), BpSetCommandText);
}

static CMDRESULT cbDebugSetBPXCommandConditionCommon(BP_TYPE Type, int argc, char* argv[])
{
    return cbDebugSetBPXTextCommon(Type, argc, argv, String(GuiTranslateText(QT_TRANSLATE_NOOP("DBG", "command condition"))), BpSetCommandCondition);
}

static CMDRESULT cbDebugGetBPXHitCountCommon(BP_TYPE Type, int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!\n"));
        return STATUS_ERROR;
    }
    BREAKPOINT bp;
    if(!BpGetAny(Type, argv[1], &bp))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    varset("$result", bp.hitcount, false);
    return STATUS_CONTINUE;

}

static CMDRESULT cbDebugResetBPXHitCountCommon(BP_TYPE Type, int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!\n"));
        return STATUS_ERROR;
    }
    duint value = 0;
    if(argc > 2)
        if(!valfromstring(argv[2], &value, false))
            return STATUS_ERROR;
    BREAKPOINT bp;
    if(!BpGetAny(Type, argv[1], &bp))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!BpResetHitCount(bp.addr, Type, (uint32)value))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Can't set hit count on breakpoint \"%s\""), argv[1]);
        return STATUS_ERROR;
    }
    DebugUpdateBreakpointsViewAsync();
    return STATUS_CONTINUE;
}

static CMDRESULT cbDebugSetBPXFastResumeCommon(BP_TYPE Type, int argc, char* argv[])
{
    BREAKPOINT bp;
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!\n"));
        return STATUS_ERROR;
    }
    auto fastResume = true;
    if(argc > 2)
    {
        duint value;
        if(!valfromstring(argv[2], &value, false))
            return STATUS_ERROR;
        fastResume = value != 0;
    }
    if(!BpGetAny(Type, argv[1], &bp))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!BpSetFastResume(bp.addr, Type, fastResume))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Can't set fast resume on breakpoint \"%1\""), argv[1]);
        return STATUS_ERROR;
    }
    DebugUpdateBreakpointsViewAsync();
    return STATUS_CONTINUE;
}

static CMDRESULT cbDebugSetBPXSingleshootCommon(BP_TYPE Type, int argc, char* argv[])
{
    BREAKPOINT bp;
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!\n"));
        return STATUS_ERROR;
    }
    auto singleshoot = true;
    if(argc > 2)
    {
        duint value;
        if(!valfromstring(argv[2], &value, false))
            return STATUS_ERROR;
        singleshoot = value != 0;
    }
    if(!BpGetAny(Type, argv[1], &bp))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!BpSetSingleshoot(bp.addr, Type, singleshoot))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Can't set singleshoot on breakpoint \"%1\""), argv[1]);
        return STATUS_ERROR;
    }
    DebugUpdateBreakpointsViewAsync();
    return STATUS_CONTINUE;
}

static CMDRESULT cbDebugSetBPXSilentCommon(BP_TYPE Type, int argc, char* argv[])
{
    BREAKPOINT bp;
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!\n"));
        return STATUS_ERROR;
    }
    auto silent = true;
    if(argc > 2)
    {
        duint value;
        if(!valfromstring(argv[2], &value, false))
            return STATUS_ERROR;
        silent = value != 0;
    }
    if(!BpGetAny(Type, argv[1], &bp))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!BpSetSilent(bp.addr, Type, silent))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Can't set silent on breakpoint \"%1\""), argv[1]);
        return STATUS_ERROR;
    }
    DebugUpdateBreakpointsViewAsync();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugSetBPXName(int argc, char* argv[])
{
    return cbDebugSetBPXNameCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXCondition(int argc, char* argv[])
{
    return cbDebugSetBPXConditionCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXLog(int argc, char* argv[])
{
    return cbDebugSetBPXLogCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXLogCondition(int argc, char* argv[])
{
    return cbDebugSetBPXLogConditionCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXCommand(int argc, char* argv[])
{
    return cbDebugSetBPXCommandCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXCommandCondition(int argc, char* argv[])
{
    return cbDebugSetBPXCommandConditionCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXFastResume(int argc, char* argv[])
{
    return cbDebugSetBPXFastResumeCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXSingleshoot(int argc, char* argv[])
{
    return cbDebugSetBPXSingleshootCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXSilent(int argc, char* argv[])
{
    return cbDebugSetBPXSilentCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugResetBPXHitCount(int argc, char* argv[])
{
    return cbDebugResetBPXHitCountCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugGetBPXHitCount(int argc, char* argv[])
{
    return cbDebugGetBPXHitCountCommon(BPNORMAL, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareCondition(int argc, char* argv[])
{
    return cbDebugSetBPXConditionCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareLog(int argc, char* argv[])
{
    return cbDebugSetBPXLogCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareLogCondition(int argc, char* argv[])
{
    return cbDebugSetBPXLogConditionCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareCommand(int argc, char* argv[])
{
    return cbDebugSetBPXCommandCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareCommandCondition(int argc, char* argv[])
{
    return cbDebugSetBPXCommandConditionCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareFastResume(int argc, char* argv[])
{
    return cbDebugSetBPXFastResumeCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareSingleshoot(int argc, char* argv[])
{
    return cbDebugSetBPXSingleshootCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXHardwareSilent(int argc, char* argv[])
{
    return cbDebugSetBPXSilentCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugResetBPXHardwareHitCount(int argc, char* argv[])
{
    return cbDebugResetBPXHitCountCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugGetBPXHardwareHitCount(int argc, char* argv[])
{
    return cbDebugGetBPXHitCountCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetBPXMemoryCondition(int argc, char* argv[])
{
    return cbDebugSetBPXConditionCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemoryLog(int argc, char* argv[])
{
    return cbDebugSetBPXLogCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemoryLogCondition(int argc, char* argv[])
{
    return cbDebugSetBPXLogConditionCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemoryCommand(int argc, char* argv[])
{
    return cbDebugSetBPXCommandCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemoryCommandCondition(int argc, char* argv[])
{
    return cbDebugSetBPXCommandConditionCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugResetBPXMemoryHitCount(int argc, char* argv[])
{
    return cbDebugResetBPXHitCountCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemoryFastResume(int argc, char* argv[])
{
    return cbDebugSetBPXFastResumeCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemorySingleshoot(int argc, char* argv[])
{
    return cbDebugSetBPXSingleshootCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXMemorySilent(int argc, char* argv[])
{
    return cbDebugSetBPXSilentCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugGetBPXMemoryHitCount(int argc, char* argv[])
{
    return cbDebugGetBPXHitCountCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLCondition(int argc, char* argv[])
{
    return cbDebugSetBPXConditionCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLLog(int argc, char* argv[])
{
    return cbDebugSetBPXLogCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLLogCondition(int argc, char* argv[])
{
    return cbDebugSetBPXLogConditionCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLCommand(int argc, char* argv[])
{
    return cbDebugSetBPXCommandCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLCommandCondition(int argc, char* argv[])
{
    return cbDebugSetBPXCommandConditionCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugResetBPXDLLHitCount(int argc, char* argv[])
{
    return cbDebugResetBPXHitCountCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLFastResume(int argc, char* argv[])
{
    return cbDebugSetBPXFastResumeCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLSingleshoot(int argc, char* argv[])
{
    return cbDebugSetBPXSingleshootCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLSilent(int argc, char* argv[])
{
    return cbDebugSetBPXSilentCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugGetBPXDLLHitCount(int argc, char* argv[])
{
    return cbDebugGetBPXHitCountCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugSetBPGoto(int argc, char* argv[])
{
    if(argc != 3)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "argument count mismatch!\n"));
        return STATUS_ERROR;
    }
    char cmd[deflen];
    _snprintf(cmd, sizeof(cmd), "SetBreakpointCondition %s, 0", argv[1]);
    if(!_dbg_dbgcmddirectexec(cmd))
        return STATUS_ERROR;
    _snprintf(cmd, sizeof(cmd), "SetBreakpointCommand %s, \"CIP=%s\"", argv[1], argv[2]);
    if(!_dbg_dbgcmddirectexec(cmd))
        return STATUS_ERROR;
    _snprintf(cmd, sizeof(cmd), "SetBreakpointCommandCondition %s, 1", argv[1]);
    if(!_dbg_dbgcmddirectexec(cmd))
        return STATUS_ERROR;
    _snprintf(cmd, sizeof(cmd), "SetBreakpointFastResume %s, 0", argv[1]);
    if(!_dbg_dbgcmddirectexec(cmd))
        return STATUS_ERROR;
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugSetHardwareBreakpoint(int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "not enough arguments!"));
        return STATUS_ERROR;
    }
    duint addr;
    if(!valfromstring(argv[1], &addr))
        return STATUS_ERROR;
    DWORD type = UE_HARDWARE_EXECUTE;
    if(argc > 2)
    {
        switch(*argv[2])
        {
        case 'r':
            type = UE_HARDWARE_READWRITE;
            break;
        case 'w':
            type = UE_HARDWARE_WRITE;
            break;
        case 'x':
            break;
        default:
            dputs(QT_TRANSLATE_NOOP("DBG", "Invalid type, assuming 'x'"));
            break;
        }
    }
    DWORD titsize = UE_HARDWARE_SIZE_1;
    if(argc > 3)
    {
        duint size;
        if(!valfromstring(argv[3], &size))
            return STATUS_ERROR;
        switch(size)
        {
        case 1:
            titsize = UE_HARDWARE_SIZE_1;
            break;
        case 2:
            titsize = UE_HARDWARE_SIZE_2;
            break;
        case 4:
            titsize = UE_HARDWARE_SIZE_4;
            break;
#ifdef _WIN64
        case 8:
            titsize = UE_HARDWARE_SIZE_8;
            break;
#endif // _WIN64
        default:
            titsize = UE_HARDWARE_SIZE_1;
            dputs(QT_TRANSLATE_NOOP("DBG", "Invalid size, using 1"));
            break;
        }
        if((addr % size) != 0)
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Address not aligned to %d\n"), size);
            return STATUS_ERROR;
        }
    }
    DWORD drx = 0;
    if(!GetUnusedHardwareBreakPointRegister(&drx))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "You can only set 4 hardware breakpoints"));
        return STATUS_ERROR;
    }
    int titantype = 0;
    TITANSETDRX(titantype, drx);
    TITANSETTYPE(titantype, type);
    TITANSETSIZE(titantype, titsize);
    //TODO: hwbp in multiple threads TEST
    BREAKPOINT bp;
    if(BpGet(addr, BPHARDWARE, 0, &bp))
    {
        if(!bp.enabled)
            return DbgCmdExecDirect(StringUtils::sprintf("bphwe %p", bp.addr).c_str()) ? STATUS_CONTINUE : STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint already set!"));
        return STATUS_CONTINUE;
    }
    if(!BpNew(addr, true, false, 0, BPHARDWARE, titantype, 0))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Error setting hardware breakpoint (bpnew)!"));
        return STATUS_ERROR;
    }
    if(!SetHardwareBreakPoint(addr, drx, type, titsize, (void*)cbHardwareBreakpoint))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Error setting hardware breakpoint (TitanEngine)!"));
        return STATUS_ERROR;
    }
    dprintf(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint at %p set!\n"), addr);
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugDeleteHardwareBreakpoint(int argc, char* argv[])
{
    if(argc < 2)  //delete all breakpoints
    {
        if(!BpGetCount(BPHARDWARE))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No hardware breakpoints to delete!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbDeleteAllHardwareBreakpoints))  //at least one deletion failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All hardware breakpoints deleted!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    if(BpGet(0, BPHARDWARE, argv[1], &found))  //found a breakpoint with name
    {
        if(!BpDelete(found.addr, BPHARDWARE))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Delete hardware breakpoint failed: %p (BpDelete)\n"), found.addr);
            return STATUS_ERROR;
        }
        if(!DeleteHardwareBreakPoint(TITANGETDRX(found.titantype)))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Delete hardware breakpoint failed: %p (DeleteHardwareBreakPoint)\n"), found.addr);
            return STATUS_ERROR;
        }
        return STATUS_CONTINUE;
    }
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPHARDWARE, 0, &found))  //invalid breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such hardware breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!BpDelete(found.addr, BPHARDWARE))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Delete hardware breakpoint failed: %p (BpDelete)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!DeleteHardwareBreakPoint(TITANGETDRX(found.titantype)))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Delete hardware breakpoint failed: %p (DeleteHardwareBreakPoint)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint deleted!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugEnableHardwareBreakpoint(int argc, char* argv[])
{
    DWORD drx = 0;
    if(!GetUnusedHardwareBreakPointRegister(&drx))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "You can only set 4 hardware breakpoints"));
        return STATUS_ERROR;
    }
    if(argc < 2)  //enable all hardware breakpoints
    {
        if(!BpGetCount(BPHARDWARE))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No hardware breakpoints to enable!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbEnableAllHardwareBreakpoints))  //at least one enable failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All hardware breakpoints enabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPHARDWARE, 0, &found))  //invalid hardware breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such hardware breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(found.enabled)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint already enabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    TITANSETDRX(found.titantype, drx);
    BpSetTitanType(found.addr, BPHARDWARE, found.titantype);
    if(!SetHardwareBreakPoint(found.addr, drx, TITANGETTYPE(found.titantype), TITANGETSIZE(found.titantype), (void*)cbHardwareBreakpoint))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable hardware breakpoint %p (SetHardwareBreakpoint)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!BpEnable(found.addr, BPHARDWARE, true))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable hardware breakpoint %p (BpEnable)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint enabled!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugDisableHardwareBreakpoint(int argc, char* argv[])
{
    if(argc < 2)  //delete all hardware breakpoints
    {
        if(!BpGetCount(BPHARDWARE))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No hardware breakpoints to disable!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbDisableAllHardwareBreakpoints))  //at least one deletion failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All hardware breakpoints disabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPHARDWARE, 0, &found))  //invalid hardware breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such hardware breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!found.enabled)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint already disabled!"));
        return STATUS_CONTINUE;
    }
    if(!BpEnable(found.addr, BPHARDWARE, false))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable hardware breakpoint %p (BpEnable)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!DeleteHardwareBreakPoint(TITANGETDRX(found.titantype)))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable hardware breakpoint %p (DeleteHardwareBreakpoint)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Hardware breakpoint disabled!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugSetBPXHardwareName(int argc, char* argv[])
{
    return cbDebugSetBPXNameCommon(BPHARDWARE, argc, argv);
}

CMDRESULT cbDebugSetMemoryBpx(int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Not enough arguments!"));
        return STATUS_ERROR;
    }
    duint addr;
    if(!valfromstring(argv[1], &addr))
        return STATUS_ERROR;
    bool restore = false;
    char arg3[deflen] = "";
    if(argc > 3)
        strcpy_s(arg3, argv[3]);
    if(argc > 2)
    {
        if(*argv[2] == '1')
            restore = true;
        else if(*argv[2] == '0')
            restore = false;
        else
            strcpy_s(arg3, argv[2]);
    }
    DWORD type = UE_MEMORY;
    if(*arg3)
    {
        switch(*arg3)
        {
        case 'r':
            type = UE_MEMORY_READ;
            break;
        case 'w':
            type = UE_MEMORY_WRITE;
            break;
        case 'x':
            type = UE_MEMORY_EXECUTE; //EXECUTE
            break;
        default:
            dputs(QT_TRANSLATE_NOOP("DBG", "Invalid type (argument ignored)"));
            break;
        }
    }
    duint size = 0;
    duint base = MemFindBaseAddr(addr, &size, true);
    bool singleshoot = false;
    if(!restore)
        singleshoot = true;
    BREAKPOINT bp;
    if(BpGet(base, BPMEMORY, 0, &bp))
    {
        if(!bp.enabled)
            return BpEnable(base, BPMEMORY, true) ? STATUS_CONTINUE : STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "Memory breakpoint already set!"));
        return STATUS_CONTINUE;
    }
    if(!BpNew(base, true, singleshoot, 0, BPMEMORY, type, 0))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Error setting memory breakpoint! (BpNew)"));
        return STATUS_ERROR;
    }
    if(!SetMemoryBPXEx(base, size, type, restore, (void*)cbMemoryBreakpoint))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Error setting memory breakpoint! (SetMemoryBPXEx)"));
        return STATUS_ERROR;
    }
    dprintf(QT_TRANSLATE_NOOP("DBG", "Memory breakpoint at %p set!\n"), addr);
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugDeleteMemoryBreakpoint(int argc, char* argv[])
{
    if(argc < 2)  //delete all breakpoints
    {
        if(!BpGetCount(BPMEMORY))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "no memory breakpoints to delete!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbDeleteAllMemoryBreakpoints))  //at least one deletion failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All memory breakpoints deleted!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    if(BpGet(0, BPMEMORY, argv[1], &found))  //found a breakpoint with name
    {
        duint size;
        MemFindBaseAddr(found.addr, &size);
        if(!BpDelete(found.addr, BPMEMORY))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Delete memory breakpoint failed: %p (BpDelete)\n"), found.addr);
            return STATUS_ERROR;
        }
        if(!RemoveMemoryBPX(found.addr, size))
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Delete memory breakpoint failed: %p (RemoveMemoryBPX)\n"), found.addr);
            return STATUS_ERROR;
        }
        return STATUS_CONTINUE;
    }
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPMEMORY, 0, &found))  //invalid breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such memory breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    duint size;
    MemFindBaseAddr(found.addr, &size);
    if(!BpDelete(found.addr, BPMEMORY))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Delete memory breakpoint failed: %p (BpDelete)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!RemoveMemoryBPX(found.addr, size))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Delete memory breakpoint failed: %p (RemoveMemoryBPX)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Memory breakpoint deleted!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugEnableMemoryBreakpoint(int argc, char* argv[])
{
    if(argc < 2)  //enable all memory breakpoints
    {
        if(!BpGetCount(BPMEMORY))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No memory breakpoints to enable!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbEnableAllMemoryBreakpoints))  //at least one enable failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All memory breakpoints enabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPMEMORY, 0, &found))  //invalid memory breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such memory breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(found.enabled)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Memory memory already enabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    duint size = 0;
    MemFindBaseAddr(found.addr, &size);
    if(!SetMemoryBPXEx(found.addr, size, found.titantype, !found.singleshoot, (void*)cbMemoryBreakpoint))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable memory breakpoint %p (SetMemoryBPXEx)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!BpEnable(found.addr, BPMEMORY, true))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not enable memory breakpoint %p (BpEnable)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Memory breakpoint enabled!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugDisableMemoryBreakpoint(int argc, char* argv[])
{
    if(argc < 2)  //delete all memory breakpoints
    {
        if(!BpGetCount(BPMEMORY))
        {
            dputs(QT_TRANSLATE_NOOP("DBG", "No memory breakpoints to disable!"));
            return STATUS_CONTINUE;
        }
        if(!BpEnumAll(cbDisableAllMemoryBreakpoints))  //at least one deletion failed
            return STATUS_ERROR;
        dputs(QT_TRANSLATE_NOOP("DBG", "All memory breakpoints disabled!"));
        GuiUpdateAllViews();
        return STATUS_CONTINUE;
    }
    BREAKPOINT found;
    duint addr = 0;
    if(!valfromstring(argv[1], &addr) || !BpGet(addr, BPMEMORY, 0, &found))  //invalid memory breakpoint
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "No such memory breakpoint \"%s\"\n"), argv[1]);
        return STATUS_ERROR;
    }
    if(!found.enabled)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Memory breakpoint already disabled!"));
        return STATUS_CONTINUE;
    }
    duint size = 0;
    MemFindBaseAddr(found.addr, &size);
    if(!RemoveMemoryBPX(found.addr, size))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable memory breakpoint %p (RemoveMemoryBPX)\n"), found.addr);
        return STATUS_ERROR;
    }
    if(!BpEnable(found.addr, BPMEMORY, false))
    {
        dprintf(QT_TRANSLATE_NOOP("DBG", "Could not disable memory breakpoint %p (BpEnable)\n"), found.addr);
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "Memory breakpoint disabled!"));
    GuiUpdateAllViews();
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugSetBPXMemoryName(int argc, char* argv[])
{
    return cbDebugSetBPXNameCommon(BPMEMORY, argc, argv);
}

CMDRESULT cbDebugSetBPXDLLName(int argc, char* argv[])
{
    return cbDebugSetBPXNameCommon(BPDLL, argc, argv);
}

CMDRESULT cbDebugBplist(int argc, char* argv[])
{
    if(!BpEnumAll(cbBreakpointList))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Something went wrong..."));
        return STATUS_ERROR;
    }
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugBpDll(int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Not enough arguments!"));
        return STATUS_ERROR;
    }
    DWORD type = UE_ON_LIB_ALL;
    if(argc > 2)
    {
        switch(*argv[2])
        {
        case 'l':
            type = UE_ON_LIB_LOAD;
            break;
        case 'u':
            type = UE_ON_LIB_UNLOAD;
            break;
        }
    }
    bool singleshoot = true;
    if(argc > 3)
        singleshoot = false;
    if(!BpNewDll(argv[1], true, true, type, ""))
    {
        return STATUS_ERROR;
    }
    if(!LibrarianSetBreakPoint(argv[1], type, singleshoot, (void*)cbLibrarianBreakpoint))
        return STATUS_ERROR;
    dprintf(QT_TRANSLATE_NOOP("DBG", "Dll breakpoint set on \"%s\"!\n"), argv[1]);
    return STATUS_CONTINUE;
}

CMDRESULT cbDebugBcDll(int argc, char* argv[])
{
    if(argc < 2)
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Not enough arguments"));
        return STATUS_ERROR;
    }
    if(!BpDelete(ModHashFromName(argv[1]), BPDLL))
    {
        return STATUS_ERROR;
    }
    if(!LibrarianRemoveBreakPoint(argv[1], UE_ON_LIB_ALL))
    {
        dputs(QT_TRANSLATE_NOOP("DBG", "Failed to remove DLL breakpoint..."));
        return STATUS_ERROR;
    }
    dputs(QT_TRANSLATE_NOOP("DBG", "DLL breakpoint removed!"));
    return STATUS_CONTINUE;
}
