// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * stb_impl.c - the JPEG decoder for the pictures (.btx BTX3), from Sean
 * Barrett's stb_image.h (public domain / MIT, v2.30). Only JPEG, from
 * memory; no stdio, no SIMD, no thread-local storage (AmigaOS newlib).
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"
