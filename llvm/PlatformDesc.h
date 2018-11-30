#ifndef PLATFORMDESC_H
#define PLATFORMDESC_H

struct PlatformDesc {
  size_t contextSize_;
  size_t pcFieldOffset_;
  size_t prologueSize_;
  size_t directSize_;
  size_t indirectSize_;
  size_t assistSize_;
  void* opaque_;
  void (*patchPrologue_)(void* opaque, uint8_t* start, uint8_t* end);
  void (*patchDirect_)(void* opaque, uint8_t* toFill);
  void (*patchIndirect_)(void* opaque, uint8_t* toFill);
  void (*patchAssist_)(void* opaque, uint8_t* toFill);
};

#endif /* PLATFORMDESC_H */
