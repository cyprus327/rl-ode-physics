#include <stdio.h>

#include "../inc/rand.h"

u32 randState = 0;

u32 Rand_Next(void) {
    randState += 0xE120FC15;
    u64 temp = (u64)randState * 0x4A39B70D;
    const u32 m1 = (u32)((temp >> 32) ^ temp);
    temp = (u64)m1 * 0x12FAD5C9;
    return (u32)((temp >> 32) ^ temp);
}

i32 Rand_Int(i32 min, i32 max) {
    if (min >= max) {
        printf("Min >= Max (%d, %d)\n", min, max);
        return 0;
    }

    return (i32)(Rand_Next() % (max - min)) + min;
}

f64 Rand_Double(f64 min, f64 max) {
    if (min >= max) {
        printf("Min >= Max (%f, %f)\n", min, max);
    }

    return (f64)(min + Rand_Next() / (f64)0xFFFFFFFF * ((f64)max - (f64)min));
}
