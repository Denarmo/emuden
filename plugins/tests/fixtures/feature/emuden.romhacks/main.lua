-- SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
-- SPDX-License-Identifier: GPL-3.0-or-later

-- ROM Hacks, stage 1 (docs/feature-plugins.md, section 8.1): the user downloads a patch in
-- their own browser, this plugin finds the base game in the library and installs the hack.
-- It makes no request to any site and never downloads a game.

local pages = den.include("pages.lua")

local plugin = { actions = {} }

-- The patch being installed: { path, name, info, base, title }.
local pending = nil

local function title_of(path)
  local name = path:match("([^/]+)$") or path
  name = name:gsub("%.[%w]+$", "")
  name = name:gsub("[_%.]+", " ")
  return name
end

local function show_home()
  den.ui.set(pages.home(den.files.inbox() or {}, den.library.hacks()))
end

local function begin_install(path)
  local info, why = den.patch.inspect(path)
  if not info then
    return den.ui.toast(why or "This file is not a patch.")
  end
  local base = nil
  if info.source_crc32 then
    base = den.library.find{ crc32 = info.source_crc32 }
  end
  pending = { path = path, info = info, base = base, title = title_of(path) }
  if base then
    den.ui.push(pages.install(pending))
  elseif info.source_crc32 then
    den.ui.push(pages.missing_base(info))
  else
    -- A format without a checksum of its base (xdelta, ips): the user says which game.
    den.ui.push(pages.choose_base(den.library.games() or {}))
  end
end

function plugin.open()
  show_home()
end

function plugin.resume()
  if pending == nil then
    show_home()
  end
end

function plugin.file(path)
  begin_install(path)
end

function plugin.actions.browse()
  den.open_url("https://www.hackdex.app/discover")
end

function plugin.actions.import()
  local path = den.files.import{ title = "Choose a patch file" }
  if path then
    begin_install(path)
  end
end

function plugin.actions.take(token)
  local path, why = den.files.take(token)
  if not path then
    return den.ui.toast(why)
  end
  begin_install(path)
end

function plugin.actions.base(id)
  if pending == nil then
    return
  end
  for _, game in ipairs(den.library.games() or {}) do
    if game.id == id then
      pending.base = game
    end
  end
  if pending.base then
    den.ui.pop()
    den.ui.push(pages.install(pending))
  end
end

function plugin.actions.title(_, value)
  if pending and value and value ~= "" then
    pending.title = value
    den.ui.patch("title", { value = value })
  end
end

function plugin.actions.install()
  if pending == nil or pending.base == nil then
    return
  end
  den.ui.patch("status", { label = "Patching " .. pending.base.name, fraction = -1 })
  local hack, why = den.patch.apply{
    patch = pending.path,
    game = pending.base.id,
    title = pending.title,
    source = "file",
  }
  if not hack then
    den.ui.patch("status", { label = why, fraction = 0 })
    return den.ui.toast(why)
  end
  den.files.remove(pending.path)
  pending = nil
  den.ui.pop()
  show_home()
  den.ui.toast("Installed. It is in your library.")
end

function plugin.actions.hack(id)
  for _, hack in ipairs(den.library.hacks()) do
    if hack.id == id then
      return den.ui.push(pages.hack(hack))
    end
  end
end

function plugin.actions.play(id)
  den.app.open_game(id)
end

function plugin.actions.rename(id, value)
  if value and value ~= "" then
    local ok, why = den.library.update(id, { title = value })
    den.ui.toast(ok and "Renamed." or why)
  end
end

function plugin.actions.remove(id)
  if den.ui.confirm{ title = "Remove this hack?", message = "Its patched file is deleted. " ..
                     "Your own game stays.", yes = "Remove" } then
    local ok, why = den.library.remove(id)
    if not ok then
      return den.ui.toast(why)
    end
    den.ui.pop()
    show_home()
  end
end

return plugin
