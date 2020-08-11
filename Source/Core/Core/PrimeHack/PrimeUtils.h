#pragma once

#include "Core/PrimeHack/PrimeMod.h"

#include <cmath>
#include <sstream>

#include "Core/PowerPC/PowerPC.h"
#include "Core/PrimeHack/HackConfig.h"
#include "InputCommon/GenericMouse.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/VideoCommon.h"

extern std::string cplayer_str;

namespace prime
{
constexpr float TURNRATE_RATIO = 0.00498665500569808449206349206349f;


struct GameCursor
{
  bool is_set;
  u32 x_address;
  u32 y_address;
  float right_bound;
  float bottom_bound;
  Region region;

  void load();
  void process();
};

extern GameCursor game_cursor;

void load_game_cursor(Region p_region);
int get_beam_switch(std::array<int, 4> const& beams);
std::tuple<int, int> get_visor_switch(std::array<std::tuple<int, int>, 4> const& visors, bool combat_visor);

void handle_cursor(u32 x_address, u32 y_address, float right_bound, float bottom_bound);

void springball_code(u32 base_offset, std::vector<CodeChange>* code_changes);
void springball_check(u32 ball_address, u32 movement_address);

void set_cplayer_str(u32 address);

bool mem_check(u32 address);
void write_invalidate(u32 address, u32 value);
void write_if_different(u32 address, u32 value);
float getAspectRatio();

void set_beam_owned(int index, bool owned);
void set_visor_owned(int index, bool owned);
void set_cursor_pos(float x, float y);

void disable_culling(u32 address);
void adjust_viewmodel(float fov, u32 arm_address, u32 znear_address, u32 znear_value);




class MenuNTSC : public PrimeMod
{
public:
  Game game() const override { return Game::MENU; }

  Region region() const override { return Region::NTSC; }

  void run_mod() override;

  virtual ~MenuNTSC() {}
};

class MenuPAL : public PrimeMod
{
public:
  Game game() const override { return Game::MENU; }

  Region region() const override { return Region::PAL; }

  void run_mod() override;

  virtual ~MenuPAL() {}
};
}  // namespace prime
