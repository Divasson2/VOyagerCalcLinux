/* sha256.h - minimal SHA-256 (FIPS 180-4), written fresh. Used only to recognise firmware files. */
/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#ifndef VC_SHA256_H
#define VC_SHA256_H
#include <stdint.h>
#include <stddef.h>
void vc_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
#endif
