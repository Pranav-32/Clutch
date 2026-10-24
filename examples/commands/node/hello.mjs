// Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
// SPDX-License-Identifier: BSD-3-Clause
/** Print arguments exactly as received; Clutch performs no shell expansion. */
console.log(JSON.stringify({ arguments: process.argv.slice(2) }));
