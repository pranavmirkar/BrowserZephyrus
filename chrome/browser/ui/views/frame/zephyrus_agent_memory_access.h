// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_MEMORY_ACCESS_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_MEMORY_ACCESS_H_

class Profile;

namespace zephyrus::agent {

class LongTermMemory;

// The agent's long-term memory for `profile`, made on first use and owned by the
// profile. One per profile, however many windows: two windows writing one file
// from two objects is how a memory forgets.
//
// A private (off-the-record) profile gets a memory of its own that never has a
// file, so what it learns is gone when it closes and never mixes with the
// regular profile's.
LongTermMemory* GetLongTermMemory(Profile* profile);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_MEMORY_ACCESS_H_
