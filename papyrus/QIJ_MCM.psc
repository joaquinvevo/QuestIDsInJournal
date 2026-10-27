Scriptname QIJ_MCM extends SKI_ConfigBase
; Quest IDs in Journal - English SkyUI MCM
; Attach this script to the Start Game Enabled quest in the ESP.
; Settings persist in the INI through native functions exposed by the DLL.

Int _oidEditor
Int _oidForm
Int _oidStage
Int _oidLabelX
Int _oidLabelY
Int _oidLabelSize
Int _oidInspectorX
Int _oidInspectorY
Int _oidInspectorW
Int _oidInspectorH
Int _oidInspectorSize
Int _oidCompact
Int _oidCopy
Int _oidExecute
Int _oidHKEditor
Int _oidHKForm
Int _oidHKToggle
Int _oidHKNext
Int _oidHKPrev
Int _oidHKSelectNext
Int _oidHKSelectPrev
Int _oidHKDetails
Int _oidHKCopyStage
Int _oidHKCopyCommand
Int _oidHKRequest
Int _oidHKConfirm

Int Function GetVersion()
    Return 3
EndFunction

Event OnConfigInit()
    ModName = "Quest IDs in Journal"
    Pages = New String[4]
    Pages[0] = "Display"
    Pages[1] = "Inspector"
    Pages[2] = "Controls"
    Pages[3] = "Advanced"
EndEvent

; Refresh pages when upgrading from an existing MCM version.
Event OnVersionUpdate(Int a_version)
    ModName = "Quest IDs in Journal"
    Pages = New String[4]
    Pages[0] = "Display"
    Pages[1] = "Inspector"
    Pages[2] = "Controls"
    Pages[3] = "Advanced"
EndEvent

Event OnConfigOpen()
    If !QIJ_MCMBridge.IsReady()
        Debug.Trace("[QIJ MCM] SKSE bridge is unavailable")
    EndIf
EndEvent

Event OnPageReset(String a_page)
    ; SkyUI reuses option IDs between pages. Reset the cached IDs
    ; to prevent cross-page interactions with unrelated settings.
    _oidEditor = -1
    _oidForm = -1
    _oidStage = -1
    _oidLabelX = -1
    _oidLabelY = -1
    _oidLabelSize = -1
    _oidInspectorX = -1
    _oidInspectorY = -1
    _oidInspectorW = -1
    _oidInspectorH = -1
    _oidInspectorSize = -1
    _oidCompact = -1
    _oidCopy = -1
    _oidExecute = -1
    _oidHKEditor = -1
    _oidHKForm = -1
    _oidHKToggle = -1
    _oidHKNext = -1
    _oidHKPrev = -1
    _oidHKSelectNext = -1
    _oidHKSelectPrev = -1
    _oidHKDetails = -1
    _oidHKCopyStage = -1
    _oidHKCopyCommand = -1
    _oidHKRequest = -1
    _oidHKConfirm = -1
    SetCursorFillMode(TOP_TO_BOTTOM)
    If a_page == "" || a_page == "Display"
        AddHeaderOption("Journal Information")
        _oidEditor = AddToggleOption("Show EditorID", QIJ_MCMBridge.GetValue(1) == 1)
        _oidForm = AddToggleOption("Show FormID", QIJ_MCMBridge.GetValue(2) == 1)
        _oidStage = AddToggleOption("Show Current Stage", QIJ_MCMBridge.GetValue(3) == 1)
        AddEmptyOption()
        AddHeaderOption("Top-Right Label")
        _oidLabelX = AddSliderOption("Position X", QIJ_MCMBridge.GetValue(4) as Float, "{0}")
        _oidLabelY = AddSliderOption("Position Y", QIJ_MCMBridge.GetValue(5) as Float, "{0}")
        _oidLabelSize = AddSliderOption("Font Size", QIJ_MCMBridge.GetValue(6) as Float, "{0}")
    ElseIf a_page == "Inspector"
        AddHeaderOption("Stage Inspector Panel")
        _oidCompact = AddToggleOption("Compact Pages", QIJ_MCMBridge.GetValue(12) == 1)
        _oidInspectorX = AddSliderOption("Position X", QIJ_MCMBridge.GetValue(7) as Float, "{0}")
        _oidInspectorY = AddSliderOption("Position Y", QIJ_MCMBridge.GetValue(8) as Float, "{0}")
        _oidInspectorW = AddSliderOption("Width", QIJ_MCMBridge.GetValue(9) as Float, "{0}")
        _oidInspectorH = AddSliderOption("Height", QIJ_MCMBridge.GetValue(10) as Float, "{0}")
        _oidInspectorSize = AddSliderOption("Font Size", QIJ_MCMBridge.GetValue(11) as Float, "{0}")
        AddEmptyOption()
        AddTextOption("Configure Hotkeys", "Controls Tab")
        AddTextOption("Key Combination", "Ctrl+Shift+key")
    ElseIf a_page == "Controls"
        ; Ctrl+Shift is fixed. SkyUI captures ONLY the final key.
        SetCursorFillMode(TOP_TO_BOTTOM)
        AddHeaderOption("Copy Identifiers")
        _oidHKEditor = AddKeyMapOption("Copy EditorID", QIJ_MCMBridge.GetHotkey(1))
        _oidHKForm = AddKeyMapOption("Copy FormID", QIJ_MCMBridge.GetHotkey(2))
        AddEmptyOption()
        AddHeaderOption("Inspector Navigation")
        _oidHKToggle = AddKeyMapOption("Toggle Inspector", QIJ_MCMBridge.GetHotkey(3))
        _oidHKNext = AddKeyMapOption("Next Page", QIJ_MCMBridge.GetHotkey(4))
        _oidHKPrev = AddKeyMapOption("Previous Page", QIJ_MCMBridge.GetHotkey(5))
        _oidHKSelectNext = AddKeyMapOption("Next Stage", QIJ_MCMBridge.GetHotkey(6))
        _oidHKSelectPrev = AddKeyMapOption("Previous Stage", QIJ_MCMBridge.GetHotkey(7))
        SetCursorPosition(1)
        AddHeaderOption("Stage Actions")
        _oidHKDetails = AddKeyMapOption("List / Details", QIJ_MCMBridge.GetHotkey(8))
        _oidHKCopyStage = AddKeyMapOption("Copy Stage Number", QIJ_MCMBridge.GetHotkey(9))
        _oidHKCopyCommand = AddKeyMapOption("Copy setstage Command", QIJ_MCMBridge.GetHotkey(10))
        AddEmptyOption()
        AddHeaderOption("Execution (DANGER)")
        _oidHKRequest = AddKeyMapOption("Request Execution", QIJ_MCMBridge.GetHotkey(11))
        _oidHKConfirm = AddKeyMapOption("Confirm Execution", QIJ_MCMBridge.GetHotkey(12))
        AddTextOption("Fixed Modifiers", "Ctrl + Shift")
    ElseIf a_page == "Advanced"
        AddHeaderOption("Tools")
        _oidCopy = AddToggleOption("Enable Copying IDs and Stages", QIJ_MCMBridge.GetValue(13) == 1)
        AddEmptyOption()
        AddHeaderOption("Stage Execution (DANGER)")
        _oidExecute = AddToggleOption("Enable Stage Execution", QIJ_MCMBridge.GetValue(14) == 1)
        AddTextOption("Request / Confirm", "See INI File")
        AddTextOption("Protection", "Two-Step Confirmation")
        AddTextOption("Warning", "Use Test Saves Only")
    EndIf
EndEvent

; Map MCM option IDs to validated native setting IDs.
; The DLL validates all 12 configurable hotkeys.
Int Function HotkeySettingForOID(Int aiOption)
    If aiOption == _oidHKEditor
        Return 1
    ElseIf aiOption == _oidHKForm
        Return 2
    ElseIf aiOption == _oidHKToggle
        Return 3
    ElseIf aiOption == _oidHKNext
        Return 4
    ElseIf aiOption == _oidHKPrev
        Return 5
    ElseIf aiOption == _oidHKSelectNext
        Return 6
    ElseIf aiOption == _oidHKSelectPrev
        Return 7
    ElseIf aiOption == _oidHKDetails
        Return 8
    ElseIf aiOption == _oidHKCopyStage
        Return 9
    ElseIf aiOption == _oidHKCopyCommand
        Return 10
    ElseIf aiOption == _oidHKRequest
        Return 11
    ElseIf aiOption == _oidHKConfirm
        Return 12
    EndIf
    Return 0
EndFunction

; SkyUI DirectInput (DIK) codes for default hotkeys.
Int Function DefaultHotkeyDIK(Int aiID)
    If aiID == 1
        Return 46 ; C
    ElseIf aiID == 2
        Return 33 ; F
    ElseIf aiID == 3
        Return 23 ; I
    ElseIf aiID == 4
        Return 49 ; N
    ElseIf aiID == 5
        Return 25 ; P
    ElseIf aiID == 6
        Return 36 ; J
    ElseIf aiID == 7
        Return 37 ; K
    ElseIf aiID == 8
        Return 32 ; D
    ElseIf aiID == 9
        Return 31 ; S
    ElseIf aiID == 10
        Return 34 ; G
    ElseIf aiID == 11
        Return 18 ; E
    ElseIf aiID == 12
        Return 19 ; R
    EndIf
    Return 0
EndFunction

Event OnOptionKeyMapChange(Int aiOption, Int a_keyCode, String a_conflictControl, String a_conflictName)
    Int hotkeyID = HotkeySettingForOID(aiOption)
    If hotkeyID == 0
        Return
    EndIf
    ; Only the final key is assigned; the DLL handles Ctrl+Shift.
    ; A base-key collision with Skyrim does not imply a chord collision.
    If QIJ_MCMBridge.SetHotkey(hotkeyID, a_keyCode)
        SetKeymapOptionValue(aiOption, a_keyCode)
    Else
        ShowMessage("Hotkey unchanged. Only A-Z and 0-9 are allowed, and each action requires a unique key. Press only the final key (Ctrl+Shift is fixed).", False)
    EndIf
EndEvent

Event OnOptionDefault(Int aiOption)
    Int hotkeyID = HotkeySettingForOID(aiOption)
    If hotkeyID > 0
        Int defaultDIK = DefaultHotkeyDIK(hotkeyID)
        If QIJ_MCMBridge.SetHotkey(hotkeyID, defaultDIK)
            SetKeymapOptionValue(aiOption, defaultDIK)
        Else
            ShowMessage("Cannot restore the default hotkey because another action uses that key. Change the conflicting hotkey first.", False)
        EndIf
    EndIf
EndEvent

Int Function ToggleSettingForOID(Int aiOption)
    If aiOption == _oidEditor
        Return 1
    ElseIf aiOption == _oidForm
        Return 2
    ElseIf aiOption == _oidStage
        Return 3
    ElseIf aiOption == _oidCompact
        Return 12
    ElseIf aiOption == _oidCopy
        Return 13
    ElseIf aiOption == _oidExecute
        Return 14
    EndIf
    Return 0
EndFunction

Int Function SliderSettingForOID(Int aiOption)
    If aiOption == _oidLabelX
        Return 4
    ElseIf aiOption == _oidLabelY
        Return 5
    ElseIf aiOption == _oidLabelSize
        Return 6
    ElseIf aiOption == _oidInspectorX
        Return 7
    ElseIf aiOption == _oidInspectorY
        Return 8
    ElseIf aiOption == _oidInspectorW
        Return 9
    ElseIf aiOption == _oidInspectorH
        Return 10
    ElseIf aiOption == _oidInspectorSize
        Return 11
    EndIf
    Return 0
EndFunction

Event OnOptionSelect(Int aiOption)
    Int settingID = ToggleSettingForOID(aiOption)
    If settingID == 0
        Return
    EndIf
    Int oldValue = QIJ_MCMBridge.GetValue(settingID)
    Int nextValue = 1
    If oldValue == 1
        nextValue = 0
    EndIf
    If settingID == 14 && nextValue == 1
        If !ShowMessage("Executing quest stages can permanently alter quests and trigger scripts. Use a separate manual test save. The inspector still requires two-step confirmation. Enable execution?", True, "Enable", "Cancel")
            Return
        EndIf
    EndIf
    If QIJ_MCMBridge.SetValue(settingID, nextValue)
        SetToggleOptionValue(aiOption, nextValue == 1)
    Else
        ShowMessage("Could not save the setting to questidsinjournal.ini. Verify that the QIJ DLL is loaded.", False)
    EndIf
EndEvent

Event OnOptionSliderOpen(Int aiOption)
    Int settingID = SliderSettingForOID(aiOption)
    If settingID == 0
        Return
    EndIf
    SetSliderDialogStartValue(QIJ_MCMBridge.GetValue(settingID) as Float)
    If settingID == 4
        SetSliderDialogRange(0.0, 650.0)
        SetSliderDialogDefaultValue(505.0)
    ElseIf settingID == 5
        SetSliderDialogRange(0.0, 150.0)
        SetSliderDialogDefaultValue(0.0)
    ElseIf settingID == 6
        SetSliderDialogRange(10.0, 16.0)
        SetSliderDialogDefaultValue(13.0)
    ElseIf settingID == 7
        SetSliderDialogRange(0.0, 800.0)
        SetSliderDialogDefaultValue(250.0)
    ElseIf settingID == 8
        SetSliderDialogRange(0.0, 550.0)
        SetSliderDialogDefaultValue(150.0)
    ElseIf settingID == 9
        SetSliderDialogRange(260.0, 750.0)
        SetSliderDialogDefaultValue(500.0)
    ElseIf settingID == 10
        SetSliderDialogRange(300.0, 600.0)
        SetSliderDialogDefaultValue(330.0)
    ElseIf settingID == 11
        SetSliderDialogRange(11.0, 20.0)
        SetSliderDialogDefaultValue(15.0)
    EndIf
    SetSliderDialogInterval(1.0)
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float a_value)
    Int settingID = SliderSettingForOID(aiOption)
    If settingID == 0
        Return
    EndIf
    If QIJ_MCMBridge.SetValue(settingID, a_value as Int)
        SetSliderOptionValue(aiOption, a_value, "{0}")
    Else
        ShowMessage("Could not save this setting to the INI.", False)
    EndIf
EndEvent

Event OnOptionHighlight(Int aiOption)
    If aiOption == _oidExecute
        SetInfoText("Danger: setstage can run scripts and complete quests. Two-step confirmation is required.")
    ElseIf aiOption == _oidCompact
        SetInfoText("Reduces unused panel space on pages with fewer stages.")
    ElseIf aiOption == _oidCopy
        SetInfoText("Enables copying IDs, stages, and setstage commands to the clipboard.")
    ElseIf HotkeySettingForOID(aiOption) > 0
        SetInfoText("Choose a single letter or digit. In the journal, use Ctrl+Shift plus that key. Duplicates are not allowed. Use the option reset command for the default.")
    Else
        SetInfoText("Changes are saved to questidsinjournal.ini and applied to the journal.")
    EndIf
EndEvent
