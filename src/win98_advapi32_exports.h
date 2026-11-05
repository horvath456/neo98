// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// These ADVAPI32 exports return zero and set ERROR_CALL_NOT_IMPLEMENTED
// (120). The third column records consumed argument DWORDs. Identity and
// security variants are in src/win98_security_identity_exports.h. Generated
// by tools/advapi32_imports_audit.py.
static constexpr struct {const char*name;unsigned ordinal,encoding;} OriginalAdvapiUnsupportedExports[]={
    {"DeregisterEventSource",91,0x1}, // 409 EXEs
    {"RegisterEventSourceW",265,0x2}, // 406 EXEs
    {"ReportEventW",269,0x9}, // 406 EXEs
    {"RegSetValueExW",260,0x6}, // 308 EXEs
    {"RegQueryValueExW",248,0x6}, // 287 EXEs
    {"RegCreateKeyW",222,0x3}, // 256 EXEs
    {"RegEnumKeyW",230,0x4}, // 222 EXEs
    {"RegOpenKeyW",241,0x3}, // 222 EXEs
    {"RegQueryValueW",249,0x4}, // 221 EXEs
    {"RegOpenKeyExW",240,0x5}, // 122 EXEs
    {"RegDeleteValueW",226,0x2}, // 79 EXEs
    {"RegDeleteKeyW",224,0x2}, // 78 EXEs
    {"RegEnumValueW",232,0x8}, // 77 EXEs
    {"RegCreateKeyExW",221,0x9}, // 71 EXEs
    {"RegEnumKeyExW",229,0x8}, // 69 EXEs
    {"RegQueryInfoKeyW",243,0xc}, // 67 EXEs
    {"CloseServiceHandle",36,0x1}, // 35 EXEs
    {"OpenSCManagerA",200,0x3}, // 31 EXEs
    {"QueryServiceStatus",213,0x2}, // 28 EXEs
    {"AddAccessAllowedAce",6,0x4}, // 27 EXEs
    {"InitializeAcl",158,0x3}, // 27 EXEs
    {"OpenSCManagerW",201,0x3}, // 24 EXEs
    {"OpenServiceW",203,0x3}, // 24 EXEs
    {"GetAce",105,0x3}, // 23 EXEs
    {"RegConnectRegistryW",218,0x3}, // 21 EXEs
    {"EnumServicesStatusA",97,0x8}, // 19 EXEs
    {"RegisterEventSourceA",264,0x2}, // 15 EXEs
    {"ReportEventA",268,0x9}, // 15 EXEs
    {"OpenServiceA",202,0x3}, // 12 EXEs
    {"DeleteService",90,0x1}, // 10 EXEs
    {"ChangeServiceConfigA",31,0xb}, // 9 EXEs
    {"CreateServiceA",48,0xd}, // 9 EXEs
    {"QueryServiceConfigA",208,0x4}, // 8 EXEs
    {"IsTextUnicode",163,0x3}, // 7 EXEs
    {"LockServiceDatabase",167,0x1}, // 7 EXEs
    {"UnlockServiceDatabase",304,0x1}, // 7 EXEs
    {"SetServiceStatus",295,0x2}, // 5 EXEs
    {"ControlService",37,0x3}, // 4 EXEs
    {"StartServiceA",298,0x3}, // 4 EXEs
    {"RegisterServiceCtrlHandlerA",266,0x2}, // 3 EXEs
    {"StartServiceCtrlDispatcherA",299,0x1}, // 3 EXEs
    {"ChangeServiceConfigW",32,0xb}, // 2 EXEs
    {"CreateServiceW",49,0xd}, // 2 EXEs
    {"MakeSelfRelativeSD",183,0x3}, // 2 EXEs
    {"RegisterServiceCtrlHandlerW",267,0x2}, // 2 EXEs
    {"StartServiceCtrlDispatcherW",300,0x1}, // 2 EXEs
    {"StartServiceW",301,0x3}, // 2 EXEs
    {"AddAccessDeniedAce",7,0x4}, // 1 EXEs
    {"AddAce",8,0x5}, // 1 EXEs
    {"AddAuditAccessAce",9,0x6}, // 1 EXEs
    {"CreateProcessAsUserA",46,0xb}, // 1 EXEs
    {"EnumDependentServicesA",95,0x6}, // 1 EXEs
    {"EnumDependentServicesW",96,0x6}, // 1 EXEs
    {"GetAclInformation",106,0x4}, // 1 EXEs
    {"GetServiceDisplayNameA",140,0x4}, // 1 EXEs
    {"IsValidAcl",164,0x1}, // 1 EXEs
    {"LogonUserW",169,0x6}, // 1 EXEs
    {"MakeAbsoluteSD",182,0xb}, // 1 EXEs
    {"MapGenericMask",184,0x2}, // 1 EXEs
    {"ObjectOpenAuditAlarmW",192,0xc}, // 1 EXEs
    {"RegSetValueW",261,0x5}, // 1 EXEs
};
