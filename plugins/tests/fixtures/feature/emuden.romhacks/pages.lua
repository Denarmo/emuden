-- SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
-- SPDX-License-Identifier: GPL-3.0-or-later

-- The pages of ROM Hacks as UI models. No colours, sizes or positions: the app draws them.

local pages = {}

local function hex(number)
  return string.format("%08X", number)
end

function pages.home(inbox, hacks)
  local body = {
    { type = "text", style = "body", text =
        "1. Find a hack on Hackdex in your browser and download its patch file.\n" ..
        "2. Come back here: the patch shows under Found in Downloads, or choose the file.\n" ..
        "3. The hack is made from your own copy of the game and added to your library." },
    { type = "button", label = "Open Hackdex", action = "browse" },
    { type = "button", label = "Choose a patch file", action = "import" },
  }
  if #inbox > 0 then
    local tiles = {}
    for _, file in ipairs(inbox) do
      tiles[#tiles + 1] = { title = file.name, subtitle = "Patch file", action = "take",
                            arg = file.token }
    end
    body[#body + 1] = { type = "shelf", title = "Found in Downloads", items = tiles }
  end
  if #hacks > 0 then
    local tiles = {}
    for _, hack in ipairs(hacks) do
      tiles[#tiles + 1] = { title = hack.name, subtitle = hack.version, image = "game:" .. hack.id,
                            badge = "Hack", action = "hack", arg = hack.id }
    end
    body[#body + 1] = { type = "text", style = "heading", text = "Installed hacks" }
    body[#body + 1] = { type = "grid", items = tiles }
  else
    body[#body + 1] = { type = "empty", title = "No hacks yet",
                        text = "Hacks you install appear here and in your library." }
  end
  return { title = "ROM Hacks", subtitle = "Patches for games you own", body = body }
end

function pages.install(pending)
  local facts = {
    { label = "Base game", value = pending.base.name },
    { label = "Format", value = pending.info.format },
  }
  if pending.info.target_size then
    facts[#facts + 1] = { label = "Size", value =
        string.format("%.1f MB", pending.info.target_size / 1048576) }
  end
  return {
    title = "Install a hack",
    body = {
      { type = "input", id = "title", label = "Name", value = pending.title, action = "title" },
      { type = "detail", title = pending.title, image = pending.base.cover, facts = facts,
        buttons = { { label = "Install", action = "install" } } },
      { type = "progress", id = "status", label = "", fraction = 0 },
    },
  }
end

function pages.missing_base(info)
  return {
    title = "Base game not found",
    body = {
      { type = "text", style = "warning", text =
          "This patch needs a game that is not in your library. Add your own copy of it, " ..
          "then choose the patch again." },
      { type = "detail", title = "The game this patch was made for", facts = {
          { label = "CRC32", value = hex(info.source_crc32) },
          { label = "Size", value = info.source_size and tostring(info.source_size) or "?" },
        }, buttons = {} },
    },
  }
end

function pages.choose_base(games)
  local rows = {
    { type = "text", text = "This kind of patch does not say which game it is for. Choose it." },
  }
  for _, game in ipairs(games) do
    if not game.hack_of then
      rows[#rows + 1] = { type = "row", label = game.name, value = game.system,
                          action = "base", arg = game.id }
    end
  end
  return { title = "Which game is this patch for?", body = rows }
end

function pages.hack(hack)
  return {
    title = hack.name,
    body = {
      { type = "detail", title = hack.name, subtitle = hack.version, image = "game:" .. hack.id,
        facts = { { label = "Source", value = hack.source } },
        buttons = {
          { label = "Play", action = "play", arg = hack.id },
          { label = "Remove", action = "remove", arg = hack.id },
        } },
      { type = "input", id = "rename", label = "Rename", value = hack.name, action = "rename",
        arg = hack.id },
    },
  }
end

return pages
