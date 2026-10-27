Scriptname QIJ_MCMBridge Hidden
; Native functions implemented by questidsinjournal.dll.
; Setting IDs 1..14 are valid; SetValue rejects out-of-range values.
Int Function GetValue(Int aiID) Global Native
Bool Function SetValue(Int aiID, Int aiValue) Global Native
Bool Function IsReady() Global Native

; Step 24: SkyUI DirectInput key codes. Letters/digits only, no duplicates.
Int Function GetHotkey(Int aiID) Global Native
Bool Function SetHotkey(Int aiID, Int aiDIKCode) Global Native
