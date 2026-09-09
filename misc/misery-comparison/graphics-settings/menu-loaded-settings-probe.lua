if os.getenv("MISERY_SETTINGS_LEVEL_PROBE") == "1" then
    local stage, deadline, root = 0, 0, nil
    local function write(text)
        local file = assert(io.open("_appdata_/dx12_settings_level_probe.tsv", "ab"))
        file:write(text .. "\n"); file:close()
    end
    local function tick()
        if not _G.dx12_settings_open_loaded_menu or time_global_async() < deadline or stage >= 99 then return end
        stage, deadline = stage + 1, time_global_async() + 1200
        local ok, problem = pcall(function()
            if stage == 1 then
                root:OnButton_options_clicked()
                local ui = root.opt_dlg.dx12_settings
                assert(ui:match(ui:live()) == 1, "Fresh process does not match the saved High Native preset")
                root.opt_dlg:OnBtnAdvGraphic()
                for _, page in ipairs({"Image", "Shadows", "World", "Effects"}) do
                    ui:show_page(page)
                    for number = 1, ui.page_total do ui:show_page(page, number) end
                end
                ui:show_page("World", 2)
                ui:refresh(true)
                write("loaded_menu\tall categories and pages opened; High Native active")
            elseif stage == 2 then
                get_console():execute("screenshot")
                write("capture\tWorld page 2")
                root.opt_dlg.dx12_settings.rows[1].combo:SetCurrentID(1)
            elseif stage == 3 then
                assert(root.opt_dlg.dx12_settings.change_count == 1, "Settings did not refresh while the game was paused")
                assert(get_console():get_string("r_aa") == "dlaa", "Preview changed the live renderer")
                write("paused_refresh\tpending control recognized without applying it")
                root.opt_dlg.dx12_settings:load_current()
                root.opt_dlg.dx12_settings:show_page("Effects", 3)
                root.opt_dlg.dx12_settings:refresh(true)
            elseif stage == 4 then
                get_console():execute("screenshot")
                write("capture\tEffects page 3")
            elseif stage == 5 then
                root.opt_dlg:OnBtnCancel()
                stage = 99
                write("complete")
                get_console():execute("quit")
            end
        end)
        if not ok then
            stage = 99
            write("failed\t" .. tostring(problem))
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
