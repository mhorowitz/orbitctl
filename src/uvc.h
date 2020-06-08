#pragma once

// descriptor types

constexpr int USB_ENDPOINT_DESCRIPTOR = 0x05;
constexpr int CS_INTERFACE = 0x24;
constexpr int CS_ENDPOINT = 0x25;
constexpr int VS_LOGITECH_TYPE = 0x41;

// CS_INTERFACE subtypes

constexpr int VC_HEADER = 0x01;
constexpr int VC_INPUT_TERMINAL = 0x02;
constexpr int VC_OUTPUT_TERMINAL = 0x03;
constexpr int VC_SELECTOR_UNIT = 0x04;
constexpr int VC_PROCESSING_UNIT = 0x05;
constexpr int VC_EXTENSION_UNIT = 0x06;

// VENDOR_TYPE_LOGITECH subtypes

constexpr int VS_LOGITECH_EXTENSION_UNIT = 0x01;

// other constants

constexpr int ITT_CAMERA = 0x0201;

// requests

constexpr int UVC_SET_CUR = 0x01;

// selectors and values

constexpr int LXU_MOTOR_PANTILT_RELATIVE_CONTROL = 0x01;
constexpr int LXU_MOTOR_PANTILT_RELATIVE_CONTROL_ENABLE = 0x80;

constexpr int LXU_MOTOR_PANTILT_RESET_CONTROL = 0x02;
constexpr int LXU_MOTOR_PANTILT_RESET_CONTROL_VALUE = 0x03;

constexpr int LXU_MOTOR_FOCUS_MOTOR_CONTROL = 0x03;

constexpr int LXU_HW_CONTROL_LED1 = 0x01;
constexpr int LXU_HW_CONTROL_LED1_MODE_OFF = 0x00;
constexpr int LXU_HW_CONTROL_LED1_MODE_ON = 0x01;
constexpr int LXU_HW_CONTROL_LED1_MODE_BLINKING = 0x02;
constexpr int LXU_HW_CONTROL_LED1_MODE_AUTO = 0x03;

// structs

struct VCDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
};

struct VCInterfaceHeaderDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint16_t bcdUVC;
  uint16_t wTotalLength;
  uint32_t dwClockFrequency;
  uint8_t bInCollection;
  uint8_t baInterfaceNr;
} __attribute__((packed));

struct VCInputTerminalDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bTerminalID;
  uint16_t wTerminalType;
  uint8_t bAssocTerminal;
  uint8_t iTerminal;
} __attribute__((packed));

struct VCOutputTerminalDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bTerminalID;
  uint16_t wTerminalType;
  uint8_t bAssocTerminal;
  uint8_t bSourceId;
  uint8_t iTerminal;
} __attribute__((packed));

struct VCCameraTerminalDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bTerminalID;
  uint16_t wTerminalType;
  uint8_t bAssocTerminal;
  uint8_t iTerminal;
  uint16_t wObjectiveFocalLengthMin;
  uint16_t wObjectiveFocalLengthMax;
  uint16_t wOcularFocalLength;
  uint8_t bControlSize;
  uint8_t bmControls[3];
} __attribute__((packed));

struct VCSelectorUnitDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bUnitID;
  uint8_t bNrInPins;
  uint8_t rest[];
  // uint8_t baSourceID[bNrInPins];
  // uint8_t iSelector;
} __attribute__((packed));

struct VCProcessingUnitDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bUnitID;
  uint8_t bSourceId;
  uint16_t wMaxMultiplier;
  uint8_t bControlSize;
  uint8_t bmControls[3];
  uint8_t iProcessing;
  uint8_t bmVideoStandards;
} __attribute__((packed));

struct VCEncodingUnitDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bUnitID;
  uint8_t bSourceId;
  uint8_t iEncoding;
  uint8_t bControlSize;
  uint8_t bmControls[3];
  uint8_t bmControlsRuntime[3];
} __attribute__((packed));

struct VCExtensionUnitDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubType;
  uint8_t bUnitID;
  uint8_t guidExtensionCode[16];
  uint8_t bNumControls;
  uint8_t bNrInPins;
  uint8_t rest[];
  // uint8_t baSourceID[bNrInPins];
  // uint8_t bControlSize;
  // uint8_t bmControls[bControlSize];
  // uint8_t iExtension;
} __attribute__((packed));

struct LogitechLedRequest {
  uint8_t mode;
  uint16_t frequency; // in units of 0.05 Hz
} __attribute__((packed));

struct LogitechMotorRequest {
  uint8_t leftEnable;
  uint8_t left;
  uint8_t upEnable;
  uint8_t up;
} __attribute__((packed));
