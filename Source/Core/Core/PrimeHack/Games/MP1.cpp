#pragma once

#include "Core/PrimeHack/Games/MP1.h"

#include "Core/Core.h"

namespace prime
{
  static std::array<int, 4> prime_one_beams = {0, 2, 1, 3};

  static std::array<std::tuple<int, int>, 4> prime_one_visors = {
    std::make_tuple<int, int>(0, 0x11), std::make_tuple<int, int>(2, 0x05),
    std::make_tuple<int, int>(3, 0x09), std::make_tuple<int, int>(1, 0x0d)
  };

  void MP1::beam_change_code(uint32_t base_offset)
  {
    code_changes.emplace_back(base_offset + 0x00, 0x3c80804a);  // lis    r4, 0x804a      ; set r4 to beam change base address
    code_changes.emplace_back(base_offset + 0x04, 0x388479f0);  // addi   r4, r4, 0x79f0  ; (0x804a79f0)
    code_changes.emplace_back(base_offset + 0x08, 0x80640000);  // lwz    r3, 0(r4)       ; grab flag
    code_changes.emplace_back(base_offset + 0x0c, 0x2c030000);  // cmpwi  r3, 0           ; check if beam should change
    code_changes.emplace_back(base_offset + 0x10, 0x41820058);  // beq    0x58            ; don't attempt beam change if 0
    code_changes.emplace_back(base_offset + 0x14, 0x83440004);  // lwz    r26, 4(r4)      ; get expected beam (r25, r26 used to assign beam)
    code_changes.emplace_back(base_offset + 0x18, 0x7f59d378);  // mr     r25, r26        ; copy expected beam to other reg
    code_changes.emplace_back(base_offset + 0x1c, 0x38600000);  // li     r3, 0           ; reset flag
    code_changes.emplace_back(base_offset + 0x20, 0x90640000);  // stw    r3, 0(r4)       ;
    code_changes.emplace_back(base_offset + 0x24, 0x48000044);  // b      0x44            ; jump forward to beam assign
  }

  void MP1::run_mod()
  {
    // Beam and Visor Switching:
    u32 powerup_base = PowerPC::HostRead_U32(powerups_base_address());

    int beam_id = get_beam_switch(prime_one_beams);
    if (beam_id != -1)
    {
      PowerPC::HostWrite_U32(beam_id, new_beam_address());
      PowerPC::HostWrite_U32(1, beamchange_flag_address());
    }

    int visor_id, visor_off;
    std::tie(visor_id, visor_off) = get_visor_switch(prime_one_visors, PowerPC::HostRead_U32(powerup_base + 0x1c) == 0);
    if (visor_id != -1)
    {
      if (PowerPC::HostRead_U32(powerup_base + (visor_off * 0x08) + 0x30))
      {
        PowerPC::HostWrite_U32(visor_id, powerup_base + 0x1c);
      }
    }

    //Added by LT_Schmiddy: Beam+Visor Menu Logic.

    if (CheckBeamMenuCtl() || CheckVisorMenuCtl())
    {
      Core::DisplayMessage(std::to_string((uint32_t)PowerPC::HostRead_U32(game_cursor.x_address)).c_str(), 0.05);
      
    }


    // Start Aiming Code:
    if (PowerPC::HostRead_U32(orbit_state_address()) != 5 && PowerPC::HostRead_U8(lockon_address()))
    {
      PowerPC::HostWrite_U32(0, yaw_vel_address());
      return;
    }

    int dx = prime::GetHorizontalAxis(), dy = prime::GetVerticalAxis();
    const float compensated_sens = GetSensitivity() * TURNRATE_RATIO / 60.0f;

    pitch += static_cast<float>(dy) * -compensated_sens * (InvertedY() ? -1.f : 1.f);
    pitch = std::clamp(pitch, -1.22f, 1.22f);
    const float yaw_vel = dx * -GetSensitivity() * (InvertedX() ? -1.f : 1.f);

    PowerPC::HostWrite_U32(*reinterpret_cast<u32*>(&pitch), pitch_address());
    PowerPC::HostWrite_U32(*reinterpret_cast<u32*>(&pitch), pitch_goal_address());

    PowerPC::HostWrite_U32(0, avel_limiter_address());
    PowerPC::HostWrite_U32(*reinterpret_cast<u32 const*>(&yaw_vel), yaw_vel_address());

    for (int i = 0; i < 4; i++) {
      set_visor_owned(i , PowerPC::HostRead_U32(powerup_base + (std::get<1>(prime_one_visors[i]) * 0x8) + 0x30) ? true : false);
    }

    for (int i = 0; i < 4; i++) {
      set_beam_owned(i , PowerPC::HostRead_U32(powerup_base + (prime_one_beams[i] * 0x08) + 0x30) ? true : false);
    }

    springball_check(cplayer() + 0x2f4, cplayer() + 0x25C);


    {
      u32 camera_ptr = PowerPC::HostRead_U32(camera_pointer_address());
      u32 camera_offset = ((PowerPC::HostRead_U32(active_camera_offset_address()) >> 16) & 0x3ff)
        << 3;
      u32 camera_base = PowerPC::HostRead_U32(camera_ptr + camera_offset + 0x04);
      const float fov = std::min(GetFov(), 170.f);
      PowerPC::HostWrite_U32(*reinterpret_cast<u32 const*>(&fov), camera_base + 0x164);
      PowerPC::HostWrite_U32(*reinterpret_cast<u32 const*>(&fov), global_fov1());
      PowerPC::HostWrite_U32(*reinterpret_cast<u32 const*>(&fov), global_fov2());

      adjust_viewmodel(fov, gunpos_address(), camera_base + 0x168, 0x3d200000);
    }

    if (GetCulling() || GetFov() > 101.f)
      disable_culling(culling_address());

    if (GetEnableSecondaryGunFX())
      EnableSecondaryGunFX();

    if (GetBloom())
      write_if_different(bloom_address(), 0x4e800020);
    else
      write_if_different(bloom_address(), 0x3c03ea72);
  }

  MP1NTSC::MP1NTSC()
  {
    // This instruction change is used in all 3 games, all 3 regions. It's an update to what I believe
    // to be interpolation for player camera pitch The change is from fmuls f0, f0, f1 (0xec000072) to
    // fmuls f0, f1, f1 (0xec010072), where f0 is the output. The higher f0 is, the faster the pitch
    // *can* change in-game, and squaring f1 seems to do the job.
    code_changes.emplace_back(0x80098ee4, 0xec010072);
    code_changes.emplace_back(0x80099138, 0x60000000);
    code_changes.emplace_back(0x80183a8c, 0x60000000);
    code_changes.emplace_back(0x80183a64, 0x60000000);
    code_changes.emplace_back(0x8017661c, 0x60000000);
    code_changes.emplace_back(0x802fb5b4, 0xd23f009c);
    code_changes.emplace_back(0x8019fbcc, 0x60000000);

    // Disable Beams/Visor Menu
    //code_changes.emplace_back(0x80075CD0, 0x48000044);

    beam_change_code(0x8018e544);
    springball_code(0x801476D0, &code_changes);
  }

  uint32_t MP1NTSC::orbit_state_address() const
  {
    return 0x804d3f20;
  }
  uint32_t MP1NTSC::lockon_address() const
  {
    return 0x804c00b3;
  }
  uint32_t MP1NTSC::yaw_vel_address() const
  {
    return 0x804d3d38;
  }
  uint32_t MP1NTSC::pitch_address() const
  {
    return 0x804d3ffc;
  }
  uint32_t MP1NTSC::pitch_goal_address() const
  {
    return 0x804c10ec;
  }
  uint32_t MP1NTSC::avel_limiter_address() const
  {
    return 0x804d3d74;
  }
  uint32_t MP1NTSC::new_beam_address() const
  {
    return 0x804a79f4;
  }
  uint32_t MP1NTSC::beamchange_flag_address() const
  {
    return 0x804a79f0;
  }
  uint32_t MP1NTSC::powerups_base_address() const
  {
    return 0x804bfcd4;
  }
  uint32_t MP1NTSC::camera_pointer_address() const
  {
    return 0x804bfc30;
  }
  uint32_t MP1NTSC::cplayer() const
  {
    return 0x804d3c20;
  }
  uint32_t MP1NTSC::active_camera_offset_address() const
  {
    return 0x804c4a08;
  }
  uint32_t MP1NTSC::global_fov1() const
  {
    return 0x805c0e38;
  }
  uint32_t MP1NTSC::global_fov2() const
  {
    return 0x805c0e3c;
  }
  uint32_t MP1NTSC::culling_address() const
  {
    return 0x802C7DBC;
  }
  uint32_t MP1NTSC::gunpos_address() const
  {
    return 0x804DDAE4;
  }
  uint32_t MP1NTSC::gunfx_offset() const
  {
    return 0x8018C410 - 0x8018C6A8;
  }
  uint32_t MP1NTSC::transform_ctor_offset() const
  {
    return 0x80348D2C - 0x80348C80;
  }
  uint32_t MP1NTSC::advance_particles_offset() const
  {
    return  0x80139870 - 0x801399C0;
  }
  uint32_t MP1NTSC::bloom_address() const
  {
    return 0x80290edc;
  }

  MP1PAL::MP1PAL()
  {
    code_changes.emplace_back(0x80099068, 0xec010072);
    code_changes.emplace_back(0x800992c4, 0x60000000);
    code_changes.emplace_back(0x80183cfc, 0x60000000);
    code_changes.emplace_back(0x80183d24, 0x60000000);
    code_changes.emplace_back(0x801768b4, 0x60000000);
    code_changes.emplace_back(0x802fb84c, 0xd23f009c);
    code_changes.emplace_back(0x8019fe64, 0x60000000);

    // Disable Beams/Visor Menu
    code_changes.emplace_back(0x80075D38, 0x48000044);

    beam_change_code(0x8018e7dc);
    springball_code(0x80147820, &code_changes);
  }

  uint32_t MP1PAL::orbit_state_address() const
  {
    return 0x804d7e60;
  }
  uint32_t MP1PAL::lockon_address() const
  {
    return 0x804c3ff3;
  }
  uint32_t MP1PAL::yaw_vel_address() const
  {
    return 0x804d7c78;
  }
  uint32_t MP1PAL::pitch_address() const
  {
    return 0x804d7f3c;
  }
  uint32_t MP1PAL::pitch_goal_address() const
  {
    return 0x804c502c;
  }
  uint32_t MP1PAL::avel_limiter_address() const
  {
    return 0x804d7cb4;
  }
  uint32_t MP1PAL::new_beam_address() const
  {
    return 0x804a79f4;
  }
  uint32_t MP1PAL::beamchange_flag_address() const
  {
    return 0x804a79f0;
  }
  uint32_t MP1PAL::powerups_base_address() const
  {
    return 0x804c3c14;
  }
  uint32_t MP1PAL::camera_pointer_address() const
  {
    return 0x804c3b70;
  }
  uint32_t MP1PAL::cplayer() const
  {
    return 0x804d7b60;
  }
  uint32_t MP1PAL::active_camera_offset_address() const
  {
    return 0x804c8948;
  }
  uint32_t MP1PAL::global_fov1() const
  {
    return 0x805c5178;
  }
  uint32_t MP1PAL::global_fov2() const
  {
    return 0x805c517c;
  }
  uint32_t MP1PAL::culling_address() const
  {
    return 0x802C8024;
  }
  uint32_t MP1PAL::gunpos_address() const
  {
    return 0x804E1A24;
  }
  uint32_t MP1PAL::gunfx_offset() const
  {
    return 0;
  }
  uint32_t MP1PAL::transform_ctor_offset() const
  {
    return 0;
  }
  uint32_t MP1PAL::advance_particles_offset() const
  {
    return 0;
  }
  uint32_t MP1PAL::bloom_address() const
  {
    return 0x80291258;
  }

  void MP1::EnableSecondaryGunFX()
  {
    const u32 address1 = 0x80004A68;
    const u32 address2 = 0x80004968;

    if (PowerPC::HostRead_U32(0x8018C6A8 + gunfx_offset()) == 0x4BE782C0 + gunfx_offset())
      return;

    write_invalidate(address1, 0x9421FFA8);
    write_invalidate(address1 + 0x4, 0x7C0802A6);
    write_invalidate(address1 + 0x8, 0x9001001C);
    write_invalidate(address1 + 0xc, 0x93A10010);
    write_invalidate(address1 + 0x10, 0x93C10018);
    write_invalidate(address1 + 0x14, 0x93E10014);
    write_invalidate(address1 + 0x18, 0xD3E1000C);
    write_invalidate(address1 + 0x1c, 0x7C7F1B78);
    write_invalidate(address1 + 0x20, 0x7C9E2378);
    write_invalidate(address1 + 0x24, 0x7CBD2B78);
    write_invalidate(address1 + 0x28, 0xFFE00890);
    write_invalidate(address1 + 0x2c, 0x38610020);
    write_invalidate(address1 + 0x30, 0x389F04B0);
    write_invalidate(address1 + 0x34, 0x483441E5 + transform_ctor_offset());
    write_invalidate(address1 + 0x38, 0x7FA3EB78);
    write_invalidate(address1 + 0x3c, 0x38810020);
    write_invalidate(address1 + 0x40, 0x7FE5FB78);
    write_invalidate(address1 + 0x44, 0x48134F15 + advance_particles_offset());
    write_invalidate(address1 + 0x48, 0xC03F0384);
    write_invalidate(address1 + 0x4c, 0x38800000);
    write_invalidate(address1 + 0x50, 0xC002CDF8);
    write_invalidate(address1 + 0x54, 0x807F0734);
    write_invalidate(address1 + 0x58, 0xFC010040);
    write_invalidate(address1 + 0x5c, 0x40810018);
    write_invalidate(address1 + 0x60, 0xC03F037C);
    write_invalidate(address1 + 0x64, 0xC002D078);
    write_invalidate(address1 + 0x68, 0xFC010040);
    write_invalidate(address1 + 0x6c, 0x40810008);
    write_invalidate(address1 + 0x70, 0x38800001);
    write_invalidate(address1 + 0x74, 0x81830000);
    write_invalidate(address1 + 0x78, 0x7FC5F378);
    write_invalidate(address1 + 0x7c, 0x38DF0510);
    write_invalidate(address1 + 0x80, 0xFC20F890);
    write_invalidate(address1 + 0x84, 0x818C001C);
    write_invalidate(address1 + 0x88, 0x7D8903A6);
    write_invalidate(address1 + 0x8c, 0x4E800421);
    write_invalidate(address1 + 0x90, 0x83A10010);
    write_invalidate(address1 + 0x94, 0x83C10018);
    write_invalidate(address1 + 0x98, 0x83E10014);
    write_invalidate(address1 + 0x9c, 0x8001001C);
    write_invalidate(address1 + 0xa0, 0xC3E1000C);
    write_invalidate(address1 + 0xa4, 0x7C0803A6);
    write_invalidate(address1 + 0xa8, 0x38210058);
    write_invalidate(address1 + 0xac, 0x4E800020);

    write_invalidate(address2, 0x7F03C378);
    write_invalidate(address2 + 0x4, 0x7F24CB78);
    write_invalidate(address2 + 0x8, 0x7F65DB78);
    write_invalidate(address2 + 0xc, 0xFC20F890);
    write_invalidate(address2 + 0x10, 0x480000F1);
    write_invalidate(address2 + 0x14, 0x386100B0);
    write_invalidate(address2 + 0x18, 0x48187D2C + gunfx_offset());

    write_invalidate(0x8018C6A8 + gunfx_offset(), 0x4BE782C0 - gunfx_offset());
  }
}
