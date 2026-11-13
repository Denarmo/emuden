// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The system's own volume for media, where the platform lets an app read and set it
// (Android: the music stream). It is what another app open beside the game plays at; the
// game's own volume is a share of it (player::Session::SetVolume).

#pragma once

namespace den::pal::audio {

/// The platform has a media volume this app may set. Constant for the life of the process.
bool HasSystemVolume();
/// The highest step; the lowest is 0 (silent). 0 when there is none.
int SystemVolumeSteps();
/// The step now. The hardware keys change it too, so read it whenever it is shown.
int SystemVolume();
void SetSystemVolume(int step);

} // namespace den::pal::audio
