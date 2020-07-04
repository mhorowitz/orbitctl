#include <CoreFoundation/CoreFoundation.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include <mach/mach_error.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "uvc.h"

namespace {

std::string formatHex(uint32_t hex) {
  char buf[12];
  sprintf(buf, "0x%08x", hex);
  return buf;
}

void kernCheck(kern_return_t kerr, const char* desc) {
  if (kerr == KERN_SUCCESS) {
    return;
  }
  throw std::runtime_error(
    std::string(desc) + ": " + mach_error_string(kerr) + " (" +
    formatHex(kerr) + ")");
}

void hrCheck(HRESULT result, const char* desc) {
  if (!result) {
    return;
  }
  throw std::runtime_error(
    std::string(desc) + " failed: " + formatHex(result));
}

// Specialized to clean up T's
template <typename T>
struct StorageCleaner;

template <>
struct StorageCleaner<io_iterator_t> {
  static void clean(io_iterator_t& val) {
    kernCheck(IOObjectRelease(val), "IOObjectRelease");
  }
};

/*
io_iterator_t and io_service_t are both typedefs for unsigned int.  If
there's an incompatible opaque unsigned int out there, I'll need to
rethink this API.

template <>
struct StorageCleaner<io_service_t> {
  static void clean(io_service_t& val) {
    kernCheck(IOObjectRelease(val), "IOObjectRelease");
  }
};
*/

template <>
struct StorageCleaner<IOCFPlugInInterface**> {
  static void clean(IOCFPlugInInterface** val) {
    if (val) {
      // I could call Release, but I found one reference which said
      // don't.
//      (*val)->Release(val);
      IODestroyPlugInInterface(val);
    }
  }
};

template <>
struct StorageCleaner<IOUSBDeviceInterface**> {
  static void clean(IOUSBDeviceInterface** val) {
    if (val) {
      (*val)->Release(val);
    }
  }
};

template <>
struct StorageCleaner<IOUSBInterfaceInterface220**> {
  static void clean(IOUSBInterfaceInterface220** val) {
    if (val) {
      (*val)->Release(val);
    }
  }
};

// This is a little like a std::optional, but tailored for managing C
// values.
template <typename T>
class Storage {
public:
  Storage() {}

  Storage(nullptr_t)
    : val_{nullptr} {}

  Storage(const Storage&) = delete;
  Storage& operator=(const Storage&) = delete;

  Storage(Storage&& other)
    : val_{std::move(other.val_)}
    , isValid_(other.isValid_)
  {
    other.isValid_ = false;
  }

  Storage& operator=(Storage&& other) {
    release();
    val_ = std::move(other.val_);
    isValid_ = other.isValid_;
    other.isValid_ = false;
    return *this;
  }

  ~Storage() {
    release();
  }

  void release() {
    if (isValid_) {
      StorageCleaner<T>::clean(val_);
      isValid_ = false;
    }
  }

  bool isValid() const {
    return isValid_;
  }

  // This is used when creating a reference to set a store.  Because
  // such functions can fail, this might not result in a valid object,
  // so we don't mark it valid here.
  T& initref() {
    release();
    return val_;
  }

  T* initptr() {
    release();
    return &val_;
  }

  // Whatever called ref is assumed to throw an exception, so the
  // value will never be used.  If it is used, the intialization is
  // treated as successful.  In order to be cleaned up, it must be
  // used once.
  T& ref() {
    isValid_ = true;
    return val_;
  }

  T* ptr() {
    isValid_ = true;
    return &val_;
  }

  // This will fail to compile for a non-pointer T.  That's ok.
  typename std::remove_pointer<T>::type operator*() {
    isValid_ = true;
    return *val_;
  }

private:
  bool isValid_ = false;
  T val_;
};

template <typename T>
struct IOIteratorTraits;

// This isn't a real iterator, because it's not copyable.  It's also
// missing some trait members.
template <typename T>
class IOIterator {
public:
  IOIterator()
    : element_{nullptr}
  {}

  IOIterator(Storage<io_iterator_t> iterator)
    : iterator_(std::move(iterator))
    , element_{nullptr}
  {
    // Set element_ to start off
    ++*this;
  }

  IOIterator(IOIterator&&) = default;
  IOIterator& operator=(IOIterator&&) = default;

  bool operator==(const IOIterator& other) {
    return !iterator_.isValid() && !other.iterator_.isValid();
  }

  bool operator!=(const IOIterator& other) {
    return !(*this == other);
  }

  Storage<T**>& operator*() {
    return element_;
  }

  // prefix
  IOIterator& operator++() {
    Storage<io_service_t> service;
    while ((service.initref() = IOIteratorNext(iterator_.ref()))) {
      SInt32 score;
      Storage<IOCFPlugInInterface**> plugIn{nullptr};
      kern_return_t kerr = IOCreatePlugInInterfaceForService(
        service.ref(), IOIteratorTraits<T>::pluginType(),
        kIOCFPlugInInterfaceID, plugIn.initptr(), &score);
      if (kerr == kIOReturnNoResources) {
        // Some devices give IOKit trouble, skip them.
        continue;
      }
      kernCheck(kerr, "creating plugin interface");
      if (!plugIn.ref()) {
        throw std::runtime_error("plugIn is null");
      }

      hrCheck((*plugIn)->QueryInterface(
                plugIn.ref(),
                CFUUIDGetUUIDBytes(IOIteratorTraits<T>::interfaceID()),
                (LPVOID *)element_.initptr()),
              "QueryInterface");
      if (!element_.ref()) {
        throw std::runtime_error("device is null");
      }

      return *this;
    }

    iterator_.release();
    element_.release();

    return *this;
  }
    
  // postfix.  This increments the iterator, but returns
  // an invalid iterator.
  IOIterator operator++(int) {
    return IOIterator();
  }

private:
  friend class USBDevices;

  Storage<io_iterator_t> iterator_;
  Storage<T**> element_;
};

template <>
struct IOIteratorTraits<IOUSBDeviceInterface> {
  static CFUUIDRef pluginType() { return kIOUSBDeviceUserClientTypeID; }
  static CFUUIDRef interfaceID() { return kIOUSBDeviceInterfaceID; }
};

class USBDevices {
public:
  using Iterator = IOIterator<IOUSBDeviceInterface>;

  Iterator begin() {
    CFMutableDictionaryRef matchingDict =
      IOServiceMatching(kIOUSBDeviceClassName);
    // IOServiceGetMatchingServices decrements the refcount on
    // matchingDict, so it does not need to be otherwise released.
    Storage<io_iterator_t> iterator;
    kernCheck(IOServiceGetMatchingServices(
                kIOMasterPortDefault, matchingDict, iterator.initptr()),
              "matching services");
    return Iterator(std::move(iterator));
  }

  Iterator end() {
    return Iterator();
  }
};

void listDevices() {
  USBDevices ds;
  for (Storage<IOUSBDeviceInterface**>& device : ds) {
    UInt16 vendor, product;
    kernCheck((*device)->GetDeviceVendor(device.ref(), &vendor),
              "getting vendor");
    kernCheck((*device)->GetDeviceProduct(device.ref(), &product),
              "getting product");

    printf("found vendor 0x%04x product 0x%04x\n", vendor, product);
  }
}

Storage<IOUSBDeviceInterface**> getCamera() {
  USBDevices ds;
  for (Storage<IOUSBDeviceInterface**>& device : ds) {
    UInt16 vendor, product;
    kernCheck((*device)->GetDeviceVendor(device.ref(), &vendor),
              "getting vendor");
    kernCheck((*device)->GetDeviceProduct(device.ref(), &product),
              "getting product");

    if (vendor == 0x046d && product == 0x0994) {
      // This uses the first matching device.
      // TODO: support multiple devices.

      return std::move(device);
    }
  }

  return {};
}

class USBInterfaceOpen {
public:
  USBInterfaceOpen(IOUSBInterfaceInterface220** interface)
    : interface_(interface)
  {
    hrCheck((*interface_)->USBInterfaceOpen(interface_),
            "USBInterfaceOpen");
  }

  ~USBInterfaceOpen() {
    hrCheck((*interface_)->USBInterfaceClose(interface_),
            "USBInterfaceClose");
  }

private:
  IOUSBInterfaceInterface220** interface_;  
};

void sendControlRequest(IOUSBInterfaceInterface220** interface,
                        uint8_t interfaceNumber,
                        uint8_t unitId,
                        uint8_t selector,
                        uint8_t* data,
                        uint16_t length) {
  IOUSBDevRequest controlRequest =
    {
     .bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface),
     .bRequest = UVC_SET_CUR,
     .wValue = static_cast<UInt16>(selector << 8),
     .wIndex = static_cast<UInt16>((unitId << 8) | interfaceNumber),
     .wLength = length,
     .wLenDone = 0,
     .pData = data
    };

  USBInterfaceOpen open{interface};

  hrCheck((*interface)->ControlRequest(
            interface, /* pipeRef */ 0, &controlRequest),
          "ControlRequest");
}

class Request;

struct Camera {
  Storage<IOUSBInterfaceInterface220**> interface;
  uint8_t videoInterfaceNumber;
  uint8_t motorUnit;
  uint8_t hwControlUnit;

  bool isValid() { return interface.isValid(); }
  void send(Request& req);
};

template <>
struct IOIteratorTraits<IOUSBInterfaceInterface220> {
  static CFUUIDRef pluginType() { return kIOUSBInterfaceUserClientTypeID; }
  static CFUUIDRef interfaceID() { return kIOUSBInterfaceInterfaceID; }
};

class USBVideoInterfaces {
public:
  USBVideoInterfaces(IOUSBDeviceInterface** device)
    : device_(device) {}

  using Iterator = IOIterator<IOUSBInterfaceInterface220>;

  Iterator begin() {
    IOUSBFindInterfaceRequest videoRequest =
      {
       .bInterfaceClass = kUSBVideoInterfaceClass,
       .bInterfaceSubClass = kUSBVideoControlSubClass,
       .bInterfaceProtocol = kIOUSBFindInterfaceDontCare,
       .bAlternateSetting = kIOUSBFindInterfaceDontCare
      };

    Storage<io_iterator_t> ifIterator;
    hrCheck((*device_)->CreateInterfaceIterator(
              device_, &videoRequest, ifIterator.initptr()),
            "CreateInterfaceIterator");
    
    return Iterator(std::move(ifIterator));
  }

  Iterator end() {
    return Iterator();
  }

private:
  IOUSBDeviceInterface** device_;
};

void extractExtensionData(
    Camera& camera, const VCExtensionUnitDescriptor* eudesc) {
  constexpr uint8_t kMotorGuid[16] =
    {
     0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49,
     0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x56
    };

  constexpr uint8_t kHwControlGuid[16] =
    {
     0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49,
     0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1f
    };

  if (memcmp(
        eudesc->guidExtensionCode, kMotorGuid, sizeof(kMotorGuid)) == 0) {
    camera.motorUnit = eudesc->bUnitID;
  } else if (memcmp(
               eudesc->guidExtensionCode, kHwControlGuid,
               sizeof(kHwControlGuid)) == 0) {
    camera.hwControlUnit = eudesc->bUnitID;
  }
}
  

Camera scanDescriptors(bool display) {
  Storage<IOUSBDeviceInterface**> device = getCamera();
  if (!device.isValid()) {
    printf("No Logitech Orbit AF found\n");
    return {};
  }

  USBVideoInterfaces ifaces{device.ref()};
  auto it = ifaces.begin();
  if (it == ifaces.end()) {
    printf("No video interfaces found\n");
    return {};
  }
  
  Camera camera;
  // Just use the first matching video interface
  camera.interface = std::move(*it);
  // clean up some state
  it = std::move(ifaces.end());
  device.release();
                                           
  hrCheck((*camera.interface)->GetInterfaceNumber(
            camera.interface.ref(),
            reinterpret_cast<UInt8*>(&camera.videoInterfaceNumber)),
          "GetInterfaceNumber");
  if (display) {
    printf("Video interface number is %d\n", (int) camera.videoInterfaceNumber);
  }

  for (IOUSBDescriptorHeader *descriptor =
         (*camera.interface)->FindNextAssociatedDescriptor(
           camera.interface.ref(), NULL, kUSBAnyDesc);
       descriptor;
       descriptor =
         (*camera.interface)->FindNextAssociatedDescriptor(
           camera.interface.ref(), descriptor, kUSBAnyDesc)) {
    if (display) {
      printf("Descriptor len=%d type=%d\n",
             (int) descriptor->bLength,
             (int) descriptor->bDescriptorType);
    }

    switch (descriptor->bDescriptorType) {
    case USB_ENDPOINT_DESCRIPTOR:
      if (display) {
        printf("  USB Endpoint\n");
      }
      break;
    case CS_INTERFACE: {
      auto* vcdesc = reinterpret_cast<VCDescriptor*>(descriptor);
      switch (vcdesc->bDescriptorSubType) {
      case VC_HEADER:
        if (display) {
          printf("  VC Interface Header\n");
        }
        break;
      case VC_INPUT_TERMINAL: {
        auto* itdesc = reinterpret_cast<VCInputTerminalDescriptor*>(vcdesc);
        if (display) {
          if (itdesc->wTerminalType == ITT_CAMERA) {
            printf("  VC Camera Terminal id=%d\n", (int) itdesc->bTerminalID);
          } else {
            printf("  VC Input Terminal id=%d\n", (int) itdesc->bTerminalID);
          }
        }
        break; }
      case VC_OUTPUT_TERMINAL: {
        auto* otdesc = reinterpret_cast<VCOutputTerminalDescriptor*>(vcdesc);
        if (display) {
          printf("  VC Output Terminal id=%d\n", (int) otdesc->bTerminalID);
        }
        break; }
      case VC_SELECTOR_UNIT: {
        auto* sudesc = reinterpret_cast<VCSelectorUnitDescriptor*>(vcdesc);
        if (display) {
          printf("  VC Selector Unit id=%d\n", (int) sudesc->bUnitID);
        }
        break; }
      case VC_PROCESSING_UNIT: {
        auto* pudesc = reinterpret_cast<VCProcessingUnitDescriptor*>(vcdesc);
        if (display) {
          printf("  VC Processing Unit id=%d\n", (int) pudesc->bUnitID);
        }
        break; }
      case VC_EXTENSION_UNIT: {
        auto* eudesc = reinterpret_cast<VCExtensionUnitDescriptor*>(vcdesc);
        if (display) {
          printf("  VC Extension Unit id=%d "
                 "guid=%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                 "%02x%02x%02x%02x%02x%02x\n",
                 (int) eudesc->bUnitID,
                 (int) eudesc->guidExtensionCode[0],
                 (int) eudesc->guidExtensionCode[1],
                 (int) eudesc->guidExtensionCode[2],
                 (int) eudesc->guidExtensionCode[3],
                 (int) eudesc->guidExtensionCode[4],
                 (int) eudesc->guidExtensionCode[5],
                 (int) eudesc->guidExtensionCode[6],
                 (int) eudesc->guidExtensionCode[7],
                 (int) eudesc->guidExtensionCode[8],
                 (int) eudesc->guidExtensionCode[9],
                 (int) eudesc->guidExtensionCode[10],
                 (int) eudesc->guidExtensionCode[11],
                 (int) eudesc->guidExtensionCode[12],
                 (int) eudesc->guidExtensionCode[13],
                 (int) eudesc->guidExtensionCode[14],
                 (int) eudesc->guidExtensionCode[15]);
        }

        extractExtensionData(camera, eudesc);

        break; }
      default:
        if (display) {
          printf("  Unknown VC Interface subtype\n");
        }
        break;
      }
      break; }
    case CS_ENDPOINT:
      if (display) {
        printf("  VC Interrupt Endpoint\n");
      }
      break;
    case VS_LOGITECH_TYPE: {
      auto* vcdesc = reinterpret_cast<VCDescriptor*>(descriptor);
      switch (vcdesc->bDescriptorSubType) {
      case VS_LOGITECH_EXTENSION_UNIT: {
        auto* eudesc = reinterpret_cast<VCExtensionUnitDescriptor*>(vcdesc);
        if (display) {
          printf("  Logitech Extension Unit id=%d "
                 "guid=%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                 "%02x%02x%02x%02x%02x%02x\n",
                 (int) eudesc->bUnitID,
                 (int) eudesc->guidExtensionCode[0],
                 (int) eudesc->guidExtensionCode[1],
                 (int) eudesc->guidExtensionCode[2],
                 (int) eudesc->guidExtensionCode[3],
                 (int) eudesc->guidExtensionCode[4],
                 (int) eudesc->guidExtensionCode[5],
                 (int) eudesc->guidExtensionCode[6],
                 (int) eudesc->guidExtensionCode[7],
                 (int) eudesc->guidExtensionCode[8],
                 (int) eudesc->guidExtensionCode[9],
                 (int) eudesc->guidExtensionCode[10],
                 (int) eudesc->guidExtensionCode[11],
                 (int) eudesc->guidExtensionCode[12],
                 (int) eudesc->guidExtensionCode[13],
                 (int) eudesc->guidExtensionCode[14],
                 (int) eudesc->guidExtensionCode[15]);
        }

        extractExtensionData(camera, eudesc);

        break; }
      default:
        if (display) {
          printf("  Unknown Logitech subtype\n");
        }
      }
      break; }
    default:
      if (display) {
        printf("  Unknown descriptor type\n");
      }
    }
  }

  return std::move(camera);
}  

class Request {
public:
  // The direction is in terms of what the image appears to do.  So,
  // up would move the center of the image on the screen in the same
  // direction as if you dragged the window up.  I don't know what
  // units these are, but higher number move more.  I haven't wanted
  // to risk my device to see what happens if you exceed the range of
  // the mechanism.
  void panTiltRelative(int8_t left, int8_t up) {
    LogitechMotorRequest value;
    memset(&value, 0, sizeof(value));
    if (left != 0) {
      value.left = left < 0 ? left : left - 1;
      value.leftEnable = LXU_MOTOR_PANTILT_RELATIVE_CONTROL_ENABLE;
    }
    if (up != 0) {
      value.up = up < 0 ? up : up - 1;
      value.upEnable = LXU_MOTOR_PANTILT_RELATIVE_CONTROL_ENABLE;
    }

    setData(
      kMotorUnit,
      LXU_MOTOR_PANTILT_RELATIVE_CONTROL,
      &value,
      sizeof(value));
  }

  void panTiltReset() {
    uint8_t value = LXU_MOTOR_PANTILT_RESET_CONTROL_VALUE;

    setData(
      kMotorUnit,
      LXU_MOTOR_PANTILT_RESET_CONTROL,
      &value,
      sizeof(value));
  }

  // frequency is in units of 0.05 Hz
  void ledControl(uint8_t mode, uint16_t frequency) {
    LogitechLedRequest value =
      {
       .mode = mode,
       .frequency = CFSwapInt16HostToBig(frequency),
      };

    setData(
      kHwControlUnit,
      LXU_HW_CONTROL_LED1,
      &value,
      sizeof(value));
  }

  void send(Camera& camera) {
    uint8_t unitId;
    switch (unit_) {
    case kMotorUnit:
      unitId = camera.motorUnit;
      break;
    case kHwControlUnit:
      unitId = camera.hwControlUnit;
      break;
    default:
      throw std::runtime_error("Unknown unit");
    };

    sendControlRequest(
      camera.interface.ref(),
      camera.videoInterfaceNumber,
      unitId,
      selector_,
      data_,
      length_);
  }

private:
  enum Unit { kMotorUnit, kHwControlUnit } unit_;

  void setData(Unit unit, uint8_t selector, void *data, uint16_t length) {
    if (length > sizeof(data_)) {
      throw std::runtime_error("length cannot exceed " +
                               std::to_string(sizeof(data_)));
    }
    unit_ = unit;
    selector_ = selector;
    memcpy(data_, data, length);
    length_ = length;
  }

  uint8_t selector_;
  uint8_t data_[32];
  uint16_t length_;
};

void Camera::send(Request& req) {
  req.send(*this);
}

}

void usage() {
  fprintf(stderr,
          "usage: orbitctl cmd [opts ...]\n"
          "  scan\n"
          "  reset\n"
          "  pan left | right\n"
          "  tilt up | down\n"
          "  led on | off | auto\n");
  exit(1);
}

int main(int argc, char *argv[]) {
  if (argc < 2) usage();

  Request req;
  bool display = false;

  std::string cmd = argv[1];
  if (cmd == "scan") {
    display = true;
  } else if (cmd == "reset") {
    if (argc != 2) usage();
    req.panTiltReset();
  } else if (cmd == "pan") {
    if (argc != 3) usage();
    std::string dir = argv[2];
    if (dir == "left") {
      // Larger value arguments to panTiltRelative work, I was just
      // too lazy to write the argument parsing.
      req.panTiltRelative(1, 0);
    } else if (dir == "right") {
      req.panTiltRelative(-1, 0);
    } else {
      usage();
    }
  } else if (cmd == "tilt") {
    if (argc != 3) usage();
    std::string dir = argv[2];
    if (dir == "up") {
      req.panTiltRelative(0, 1);
    } else if (dir == "down") {
      req.panTiltRelative(0, -1);
    } else {
      usage();
    }
  } else if (cmd == "led") {
    if (argc != 3) usage();
    std::string mode = argv[2];
    if (mode == "off") {
      req.ledControl(LXU_HW_CONTROL_LED1_MODE_OFF, 0);
    } else if (mode == "on") {
      req.ledControl(LXU_HW_CONTROL_LED1_MODE_ON, 0);
    } else if (mode == "auto") {
      req.ledControl(LXU_HW_CONTROL_LED1_MODE_AUTO, 0);
    } else {
      usage();
    }
  } else {
    usage();
  }

  try {
    Camera camera = scanDescriptors(display);
    if (!camera.isValid()) {
      return 1;
    }
    if (!display) {
      camera.send(req);
    }
  } catch (const std::exception& ex) {
    std::cout << "Failure: " << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
