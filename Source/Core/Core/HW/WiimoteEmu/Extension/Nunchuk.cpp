// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>

#include "Common/BitUtils.h"
#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControllerEmu/Control/Input.h"
#include "InputCommon/ControllerEmu/ControlGroup/AnalogStick.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControlGroup/Force.h"
#include "InputCommon/ControllerEmu/ControlGroup/IMUAccelerometer.h"
#include "InputCommon/ControllerEmu/ControlGroup/Tilt.h"

namespace WiimoteEmu
{
constexpr std::array<u8, 6> nunchuk_id{{0x00, 0x00, 0xa4, 0x20, 0x00, 0x00}};

constexpr std::array<u8, 2> nunchuk_button_bitmasks{{
    Nunchuk::BUTTON_C,
    Nunchuk::BUTTON_Z,
}};

Nunchuk::Nunchuk() : Extension1stParty(_trans("Nunchuk"))
{
  // buttons
  groups.emplace_back(m_buttons = new ControllerEmu::Buttons(_trans("Buttons")));
  m_buttons->AddInput(ControllerEmu::DoNotTranslate, "C");
  m_buttons->AddInput(ControllerEmu::DoNotTranslate, "Z");

  // stick
  constexpr auto gate_radius = ControlState(STICK_GATE_RADIUS) / STICK_RADIUS;
  groups.emplace_back(m_stick =
                          new ControllerEmu::OctagonAnalogStick(_trans("Stick"), gate_radius));

  // swing
  groups.emplace_back(m_swing = new ControllerEmu::Force(_trans("Swing")));

  // tilt
  groups.emplace_back(m_tilt = new ControllerEmu::Tilt(_trans("Tilt")));

  // Shake
  // Inverse the default intensity so shake is opposite that of wiimote.
  // This is needed by DKCR for proper shake action detection.
  groups.emplace_back(m_shake = new ControllerEmu::Shake(_trans("Shake"), -1));

  // accelerometer
  groups.emplace_back(m_imu_accelerometer = new ControllerEmu::IMUAccelerometer(
                          "IMUAccelerometer", _trans("Accelerometer")));
}

void Nunchuk::Update()
{
  DataFormat nc_data = {};

  // stick
  const ControllerEmu::AnalogStick::StateData stick_state = m_stick->GetState();
  nc_data.jx = u8(STICK_CENTER + stick_state.x * STICK_RADIUS);
  nc_data.jy = u8(STICK_CENTER + stick_state.y * STICK_RADIUS);

  // Some terribly coded games check whether to move with a check like
  //
  //     if (x != 0 && y != 0)
  //         do_movement(x, y);
  //
  // With keyboard controls, these games break if you simply hit
  // of the axes. Adjust this if you're hitting one of the axes so that
  // we slightly tweak the other axis.
  if (nc_data.jx != STICK_CENTER || nc_data.jy != STICK_CENTER)
  {
    if (nc_data.jx == STICK_CENTER)
      ++nc_data.jx;
    if (nc_data.jy == STICK_CENTER)
      ++nc_data.jy;
  }

  // buttons
  u8 buttons = 0;
  m_buttons->GetState(&buttons, nunchuk_button_bitmasks.data());
  nc_data.SetButtons(buttons);

  // Acceleration data:
  EmulateSwing(&m_swing_state, m_swing, 1.f / ::Wiimote::UPDATE_FREQ);
  EmulateTilt(&m_tilt_state, m_tilt, 1.f / ::Wiimote::UPDATE_FREQ);
  EmulateShake(&m_shake_state, m_shake, 1.f / ::Wiimote::UPDATE_FREQ);

  const auto transformation =
      GetRotationalMatrix(-m_tilt_state.angle) * GetRotationalMatrix(-m_swing_state.angle);

  Common::Vec3 accel =
      transformation *
      (m_swing_state.acceleration +
       m_imu_accelerometer->GetState().value_or(Common::Vec3(0, 0, float(GRAVITY_ACCELERATION))));

  // shake
  accel += m_shake_state.acceleration;

  // Calibration values are 8-bit but we want 10-bit precision, so << 2.
  const auto acc = ConvertAccelData(accel, ACCEL_ZERO_G << 2, ACCEL_ONE_G << 2);
  nc_data.SetAccel(acc.value);

  Common::BitCastPtr<DataFormat>(&m_reg.controller_data) = nc_data;
}

void Nunchuk::Reset()
{
  EncryptedExtension::Reset();

  m_reg.identifier = nunchuk_id;

  m_swing_state = {};
  m_tilt_state = {};

  // Build calibration data:
  m_reg.calibration = {{
      // Accel Zero X,Y,Z:
      ACCEL_ZERO_G,
      ACCEL_ZERO_G,
      ACCEL_ZERO_G,
      // Possibly LSBs of zero values:
      0x00,
      // Accel 1G X,Y,Z:
      ACCEL_ONE_G,
      ACCEL_ONE_G,
      ACCEL_ONE_G,
      // Possibly LSBs of 1G values:
      0x00,
      // Stick X max,min,center:
      STICK_CENTER + STICK_GATE_RADIUS,
      STICK_CENTER - STICK_GATE_RADIUS,
      STICK_CENTER,
      // Stick Y max,min,center:
      STICK_CENTER + STICK_GATE_RADIUS,
      STICK_CENTER - STICK_GATE_RADIUS,
      STICK_CENTER,
      // 2 checksum bytes calculated below:
      0x00,
      0x00,
  }};

  UpdateCalibrationDataChecksum(m_reg.calibration, CALIBRATION_CHECKSUM_BYTES);
}

ControllerEmu::ControlGroup* Nunchuk::GetGroup(NunchukGroup group)
{
  switch (group)
  {
  case NunchukGroup::Buttons:
    return m_buttons;
  case NunchukGroup::Stick:
    return m_stick;
  case NunchukGroup::Tilt:
    return m_tilt;
  case NunchukGroup::Swing:
    return m_swing;
  case NunchukGroup::Shake:
    return m_shake;
  case NunchukGroup::IMUAccelerometer:
    return m_imu_accelerometer;
  default:
    assert(false);
    return nullptr;
  }
}

void Nunchuk::DoState(PointerWrap& p)
{
  EncryptedExtension::DoState(p);

  p.Do(m_swing_state);
  p.Do(m_tilt_state);
  p.Do(m_shake_state);
}
// Button Defaults
void Nunchuk::LoadDefaults(const ControllerInterface& ciface)
{
  // Movement
  m_stick->SetControlExpression(0, "W | UP");     // Push
  m_stick->SetControlExpression(1, "S | DOWN");   // Pull
  m_stick->SetControlExpression(2, "A | LEFT");   // Tilt left
  m_stick->SetControlExpression(3, "D | RIGHT");  // Tilt right
  // Grapple lasso controls (Only used in Prime 3)
  m_tilt->SetControlExpression(0, "`Axis Y-`");   // Swing up
  m_tilt->SetControlExpression(1, "`Axis Y+`");   // Swing dowm
  m_swing->SetControlExpression(0, "`Axis Y-`");  // Push
  m_swing->SetControlExpression(1, "`Axis Y+`");  // Pull

  // Because our defaults use keyboard input, set calibration shape to a square.
  m_stick->SetCalibrationFromGate(ControllerEmu::SquareStickGate(1.0));

  // Morph Ball
  m_buttons->SetControlExpression(0, "LCONTROL");
  // Lock/Sacn/Spider Ball
  m_buttons->SetControlExpression(1, "`Shift");

  // Shake (Only used in Prime 3, may need revision
  m_shake->SetControlExpression(1, "LSHIFT & (`Axis Y-` | `Axis Y+` | `Axis X-` | `Axis X+`)");
}
}  // namespace WiimoteEmu
