#pragma once

// Resource identifiers shared between app.rc and the C++ that loads from it.

#define IDI_APP                 101

// Built-in notification sounds, embedded as RCDATA under the custom "WAVE" type.
// The numeric order matches audio::SoundEvent so a lookup is a single addition.
#define IDW_SOUND_FIRST         200
#define IDW_SOUND_CONNECTED     200
#define IDW_SOUND_DISCONNECTED  201
#define IDW_SOUND_LOW           202
#define IDW_SOUND_CRITICAL      203
#define IDW_SOUND_CHARGED       204
#define IDW_SOUND_LAST          204
