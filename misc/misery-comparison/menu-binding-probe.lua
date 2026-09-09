-- Temporary probe using the existing native Options widgets and update hooks.
if os.getenv("MISERY_BINDING_PROBE") == "1" then
    local stage, deadline, root = 0, time_global_async() + 2000, nil
    local file = assert(io.open("_appdata_/menu_binding_probe.tsv", "wb"))
    local function write(text) file:write(text .. "\n"); file:flush() end
    write("initialized")
    local function tick()
        if time_global_async() < deadline or stage >= 6 then return end
        stage, deadline = stage + 1, time_global_async() + 1200
        write("stage\t" .. stage)
        local ok, problem = pcall(function()
            if stage == 1 then
                root:OnButton_options_clicked()
            elseif stage == 2 then
                root.opt_dlg.dx12_settings.renderer:SetTextST("st_game_over_press_jump")
            elseif stage == 3 then
                get_console():execute("screenshot")
            elseif stage == 4 then
                get_console():execute("bind jump kJ")
            elseif stage == 5 then
                get_console():execute("screenshot")
            elseif stage == 6 then
                write("complete\tdeath localization in existing Options font; original binding and J")
                file:close()
                get_console():execute("quit")
            end
        end)
        if not ok then
            write("failed\t" .. tostring(problem))
            stage = 6
            get_console():execute("quit")
        end
    end
    local main_update = main_menu.Update
    function main_menu:Update()
        if main_update then main_update(self) else CUIScriptWnd.Update(self) end
        root = self; tick()
    end
    local options_update = ui_mm_opt_main.options_dialog.Update
    function ui_mm_opt_main.options_dialog:Update()
        options_update(self)
        root = self.owner or root; tick()
    end
end