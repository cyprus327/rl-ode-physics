#pragma once

#include "raylib.h"

#include "util.h"

extern u32 randState;

u32 Rand_Next(void);
i32 Rand_Int(i32 min, i32 max);
f64 Rand_Double(f64 min, f64 max);
Color Rand_Color(u8 min, u8 max);
