/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fixed-width, pointer-free interface shared by the host and the Win32 app. */
#ifndef NEO98_HOST_DEVICES_ABI_H
#define NEO98_HOST_DEVICES_ABI_H
#define AH_VERSION 3u
#define AH_IOCTL_STATUS 0x41480001u
#define AH_IOCTL_ACTION 0x41480002u
#define AH_IOCTL_FRAME  0x41480003u
#define AH_MAX_NETWORKS 64
#define AH_MAX_DEVICES 64
#define AH_MAX_MEDIA 24
#define AH_MAX_JOYS 8
#define AH_MAX_ADAPTERS 4
#define AH_FRAME_BYTES (640u * 480u * 4u)
enum AH_Action { AH_SCAN=1, AH_CONNECT, AH_DISCONNECT, AH_WIFI_ON, AH_WIFI_OFF, AH_FORGET,
                 AH_ATTACH, AH_DETACH, AH_EJECT, AH_CAMERA_START, AH_CAMERA_STOP,
                 AH_CONNECT_HIDDEN, AH_SELECT_ADAPTER, AH_RUMBLE };
enum AH_Feature { AH_WIFI=1, AH_MEDIA=2, AH_CAMERA=4, AH_JOYSTICK=8, AH_USB=16 };
enum AH_NetworkFlags { AH_NET_ACTIVE=1, AH_NET_SAVED=2, AH_NET_SECURE=4, AH_NET_ENTERPRISE=8, AH_NET_SAE=16, AH_NET_WEP=32 };
/* AH_CONNECT_HIDDEN: ssid names the network, password the key; flags bit 0
   keeps the profile on the host, bit 1 asks for WPA personal security.
   AH_SELECT_ADAPTER: id is the index into adapter_name. */
enum AH_HiddenFlags { AH_HIDDEN_SAVE=1, AH_HIDDEN_SECURE=2 };
/* AH_RUMBLE: id is the joystick index, low and high the two motors
   (0 to 65535) and flags the duration in milliseconds. */
enum AH_JoystickFlags { AH_JOY_RUMBLE=1 };
/* What the camera does, for the Camera program: off; opening; waiting for
   the user's answer to the host's question for camera access; running
   (frames arrive); access denied by the host; failed to start (the reason
   in camera_message). */
enum AH_CameraState { AH_CAMERA_OFF=0, AH_CAMERA_OPENING, AH_CAMERA_ASKING, AH_CAMERA_RUNNING, AH_CAMERA_DENIED, AH_CAMERA_FAILED };
enum AH_MediaFlags { AH_MEDIA_OPTICAL=1, AH_MEDIA_FLOPPY=2, AH_MEDIA_READONLY=4, AH_MEDIA_MOUNTED=8 };
#pragma pack(push,4)
typedef struct AH_Power { unsigned char ac, flags, percent, reserved; unsigned seconds, full_seconds; } AH_Power;
typedef struct AH_Network { unsigned id, flags, signal, ssid_bytes; unsigned char ssid[32]; char name[128]; } AH_Network;
typedef struct AH_Device { unsigned id, vendor, product; char name[192], kind[48]; } AH_Device;
typedef struct AH_Medium { unsigned id, flags, letter; char name[192]; } AH_Medium;
typedef struct AH_Joystick { unsigned id, axes, buttons, pov; unsigned axis[6], pressed, flags; char name[128]; } AH_Joystick;
typedef struct AH_Status {
    unsigned version, features, generation, busy, last_action, error, wifi_on, wifi_connected;
    AH_Power power;
    char message[256], wifi_name[128];
    unsigned networks, devices, media, joysticks, camera_id;
    AH_Network network[AH_MAX_NETWORKS];
    AH_Device device[AH_MAX_DEVICES];
    AH_Medium medium[AH_MAX_MEDIA];
    AH_Joystick joystick[AH_MAX_JOYS];
    /* The connection of the selected adapter: kbit/s, and the IPv4 address
       with its first byte in the low byte (0 without one). */
    unsigned wifi_bitrate, wifi_address, adapters, adapter;
    char adapter_name[AH_MAX_ADAPTERS][64];
    unsigned camera_state, camera_error;
    char camera_message[256];
} AH_Status;
typedef struct AH_Request { unsigned version, action, id, flags; char password[128], ssid[33]; unsigned short low, high; } AH_Request;
typedef struct AH_Frame { unsigned width, height, stride, sequence, bytes; unsigned char pixels[AH_FRAME_BYTES]; } AH_Frame;
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(AH_Power)==12 && sizeof(unsigned)==4, "host device ABI");
#endif
#endif
