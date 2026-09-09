-- Append temporarily to the isolated menu script. Explicit environment opt-in only.
if os.getenv("MISERY_MENU_PROBE") == "1" then
    local ffi = require("ffi")
    ffi.cdef[[
        void *GetActiveWindow(void);
        int PostMessageA(void *, unsigned int, uintptr_t, intptr_t);
        long GetWindowLongA(void *, int);
    ]]
    local user32 = ffi.load("user32")
    local out = assert(io.open("_appdata_/menu_display_046_probe.tsv", "wb"))
    local stage, deadline, root = 0, time_global() + 2000, nil
    local function write(event)
        local window = user32.GetActiveWindow()
        out:write(string.format("%s\t%d\t%d\t%d\t%s\t%s\n", event, time_global(),
            device().width, device().height, get_console():get_string("vid_window_mode"),
            window ~= nil and tostring(user32.GetWindowLongA(window, -16)) or "no_active_window"))
        out:flush()
    end
    local function display(mode)
        get_console():execute("vid_window_mode " .. mode)
        get_console():execute("vid_mode 3440x1440")
        get_console():execute("vid_restart")
    end
    local function alt_enter()
        local window = assert(user32.GetActiveWindow(), "No active game window")
        assert(user32.PostMessageA(window, 0x104, 13, 0x201c0001) ~= 0)
        assert(user32.PostMessageA(window, 0x105, 13, 0xe01c0001) ~= 0)
    end
    local function tick()
        if stage >= 99 or time_global() < deadline then return end
        stage = stage + 1
        deadline = time_global() + 2500
        local ok, error = pcall(function()
            if stage == 1 then
                root:OnButton_options_clicked()
                write("options_opened")
            elseif stage == 2 then
                get_console():execute("screenshot options_windowed")
                display("st_opt_fullscreen")
            elseif stage == 3 then
                assert(device().width == 3440 and device().height == 1440)
                write("fullscreen_options")
                get_console():execute("screenshot options_fullscreen")
                display("st_opt_borderless")
            elseif stage == 4 then
                assert(device().width == 3440 and device().height == 1440)
                write("borderless_options")
                get_console():execute("screenshot options_borderless")
                display("st_opt_windowed")
            elseif stage == 5 then
                write("windowed_options")
                alt_enter()
            elseif stage == 6 then
                write("after_posted_alt_enter")
                get_console():execute("screenshot options_alt_enter")
                alt_enter()
            elseif stage == 7 then
                write("after_posted_alt_enter_return")
                display("st_opt_fullscreen")
            elseif stage == 8 then
                write("fullscreen_restored")
                assert(device().width == 3440 and device().height == 1440)
                stage = 99
                out:write("complete\n"); out:close()
                get_console():execute("quit")
            end
        end)
        if not ok then
            stage = 99
            out:write("failed\t" .. tostring(error) .. "\n"); out:close()
            get_console():execute("quit")
        end
    end
    function main_menu:Update()
        CUIScriptWnd.Update(self)
        root = self
        tick()
    end
    local options_update = ui_mm_opt_main.options_dialog.Update
    function ui_mm_opt_main.options_dialog:Update()
        if options_update then options_update(self) else CUIScriptWnd.Update(self) end
        root = self.owner or root
        tick()
    end
end
