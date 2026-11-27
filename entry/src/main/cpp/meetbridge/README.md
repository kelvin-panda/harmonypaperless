# OHOS meeting bridge source

This directory contains the HarmonyOS NAPI/bridge source used to build
`entry/libs/arm64-v8a/libmeet.so`.

## Layout
```text
meetbridge/
  CMakeLists.txt
  ohos_meet_bridge.cpp/.h   # C bridge -> libmeetcore.so
  ohos_meet_napi.cpp        # NAPI registration
  include/                  # SDK headers + cJSON/OHOS render declarations
```

The prebuilt `libmeetcore.so` / `libcodec.so` in `entry/libs/arm64-v8a`
remain the implementation of the meeting core; do not replace them with
this bridge-only build.

## Build
Use the HarmonyOS NDK CMake toolchain:

```powershell
$cmake = "D:\Huawei\DevEco Studio\sdk\default\openharmony\native\build-tools\cmake\bin\cmake.exe"
$tc = "D:\Huawei\DevEco Studio\sdk\default\openharmony\native\build\cmake\ohos.toolchain.cmake"
$ninja = "D:\Huawei\DevEco Studio\sdk\default\openharmony\native\build-tools\cmake\bin\ninja.exe"

& $cmake -S . -B build-cmake -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$tc" `
  "-DOHOS_ARCH=arm64-v8a" `
  "-DCMAKE_BUILD_TYPE=Release" `
  "-DCMAKE_MAKE_PROGRAM=$ninja"

& $cmake --build build-cmake
```

Then copy:

```text
meetbridge/build/libmeet.so
  -> entry/libs/arm64-v8a/libmeet.so
```

and rebuild the HAP with DevEco/hvigor.

## Added API
`modifyMemberRole(memberId, role)` uses:

```text
TYPE_MEET_INTERFACE_MEETSEAT
METHOD_MEET_INTERFACE_MODIFY
Item_MeetSeatDetailInfo { nameId, seatid=0, role }
```

Role values: `1=normal`, `3=compere`, `4=secretary`, `9=admin`.

The NAPI method is asynchronous (`start_async`) and is registered in
`ohos_meet_napi.cpp`; the ArkTS declaration is in
`entry/src/main/cpp/types/libmeet/index.d.ts`.
