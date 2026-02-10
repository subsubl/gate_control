#pragma once
#include <stddef.h>
#include <stdint.h>

extern "C" {
typedef struct { int x; } br_sha256_context;
void br_sha256_init(br_sha256_context* ctx) {}
void br_sha256_update(br_sha256_context* ctx, const void* data, size_t len) {}
void br_sha256_out(br_sha256_context* ctx, void* out) {}
}
