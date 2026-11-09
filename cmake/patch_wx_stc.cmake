# Applied to wxWidgets when it is fetched (the PATCH_COMMAND in CMakeLists.txt), from its source directory.
#
# wxStyledTextEvent keeps SCN_MACRORECORD's wParam and lParam in ints. Scintilla puts a pointer in lParam
# for every message that carries text, so on a 64-bit build the event hands over an address with its top
# half gone, and recording a macro crashes as soon as it reads the text. Nothing outside wx can get at the
# original: the notification is converted to this event inside a non-virtual function.
#
# Stops the configure if wx no longer reads as expected, rather than building a recorder that crashes.
set(header "include/wx/stc/stc.h")
file(READ "${header}" text)

function(widen old new)
    string(FIND "${text}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "patch_wx_stc: \"${old}\" not found in ${header}. Has wxWidgets changed?")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    set(text "${text}" PARENT_SCOPE)
endfunction()

widen("void SetWParam(int val) " "void SetWParam(wxUIntPtr val) ")
widen("void SetLParam(int val) " "void SetLParam(wxIntPtr val) ")
widen("int  GetWParam() const " "wxUIntPtr GetWParam() const ")
widen("int  GetLParam() const " "wxIntPtr GetLParam() const ")
widen("int  m_wParam;" "wxUIntPtr m_wParam;")
widen("int  m_lParam;" "wxIntPtr m_lParam;")

file(WRITE "${header}" "${text}")
