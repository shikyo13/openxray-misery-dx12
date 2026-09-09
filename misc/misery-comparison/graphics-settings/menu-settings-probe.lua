-- Temporary native menu probe; requires explicit opt-in and is removed afterward.
if os.getenv("MISERY_SETTINGS_PROBE") == "1" then
    local output = assert(io.open("_appdata_/dx12_settings_probe.tsv", "wb"))
    local stage, deadline, root, baseline = 0, time_global() + 2000, nil, nil
    local capture, capture_at
    local function write(event)
        output:write(event .. "\n"); output:flush()
    end
    local function same(a, b)
        if tonumber(a) and tonumber(b) then return math.abs(tonumber(a) - tonumber(b)) < 0.0001 end
        return a == b
    end
    local function tick()
        if capture and time_global() >= capture_at then
            get_console():execute("screenshot")
            write("capture\t" .. capture)
            capture = nil
            return
        end
        if stage >= 99 or time_global() < deadline then return end
        stage, deadline = stage + 1, time_global() + 1200
        local ok, problem = pcall(function()
            if stage == 1 then
                root:OnButton_options_clicked()
                local ui = root.opt_dlg.dx12_settings
                assert(ui.renderer:GetText() == "DirectX 12 (x64)")
                baseline = ui:live()
                for key, value in pairs(baseline) do assert(value ~= "", "Unknown control: " .. key) end
                write("opened\t" .. #ui.rows .. " rows\t" .. ui.basic_status:GetText():gsub("\n", " | "))
                -- Selecting any row is UI-only until Apply.
                for _, row in ipairs(ui.rows) do if row.combo then row.combo:SetCurrentID(0) end end
                ui:refresh(true)
                for key, value in pairs(baseline) do assert(same(value, get_console():get_string(key)), "Preview changed " .. key) end
                root.opt_dlg:OnBtnCancel()
                write("cancel\tall native console values unchanged")
            elseif stage == 2 then
                root:OnButton_options_clicked()
                local ui = root.opt_dlg.dx12_settings
                assert(ui.change_count == 0)
                ui.preset:SetCurrentID(1)
                root.opt_dlg:OnPresetChanged()
                for key, value in pairs(baseline) do assert(same(value, get_console():get_string(key)), "Preset preview changed " .. key) end
                root.opt_dlg:OnBtnAccept()
                if root.opt_dlg.message_box:IsShown() then root.opt_dlg.message_box:HideDialog() end
                write("applied\tHigh Native")
            elseif stage == 3 then
                root:OnButton_options_clicked()
                local ui = root.opt_dlg.dx12_settings
                assert(ui:match(ui:live()) == 1, "Active preset does not match High Native")
                for key, value in pairs(ui.preset_values[1]) do
                    assert(same(value, get_console():get_string(key)), "Apply mismatch: " .. key)
                    write("setting\t" .. key .. "\t" .. get_console():get_string(key))
                end
                capture, capture_at = "basic", time_global() + 400
            elseif stage >= 4 and stage <= 7 then
                root.opt_dlg:OnBtnAdvGraphic()
                local ui = root.opt_dlg.dx12_settings
                local page = ({"Image", "Shadows", "World", "Effects"})[stage - 3]
                ui:show_page(page)
                ui:refresh(true)
                capture, capture_at = page, time_global() + 400
            elseif stage == 8 then
                local ui = root.opt_dlg.dx12_settings
                ui:show_page("Image")
                ui.rows[1].combo:SetCurrentID(1) -- FXAA, individual override.
                ui:refresh(true)
                assert(ui.preset:CurrentID() == 0 and ui.change_count == 1, "Custom state not displayed")
                root.opt_dlg:OnBtnAccept()
                assert(get_console():get_string("r_aa") == "fxaa")
                root:OnButton_options_clicked()
                ui = root.opt_dlg.dx12_settings
                assert(ui:match(ui:live()) == 0)
                ui.preset:SetCurrentID(1)
                root.opt_dlg:OnPresetChanged()
                root.opt_dlg:OnBtnAccept()
                if root.opt_dlg.message_box:IsShown() then root.opt_dlg.message_box:HideDialog() end
                write("individual_apply\tFXAA saved as Custom, High Native restored")
            elseif stage == 9 then
                root:OnButton_options_clicked()
                for _, page in ipairs({"sound", "gameplay", "controls", "video"}) do
                    root.opt_dlg.tab:SetActiveTab(page)
                    root.opt_dlg:OnTabChange()
                end
                write("tabs\tsound, gameplay, controls, video opened")
                get_console():execute("cfg_save")
                stage = 99
                write("complete")
                output:close()
                get_console():execute("quit")
            end
        end)
        if not ok then
            stage = 99
            write("failed\t" .. tostring(problem))
            output:close()
            get_console():execute("quit")
        end
    end
    local main_update = main_menu.Update
    function main_menu:Update()
        if main_update then main_update(self) else CUIScriptWnd.Update(self) end
        root = self
        tick()
    end
    local options_update = ui_mm_opt_main.options_dialog.Update
    function ui_mm_opt_main.options_dialog:Update()
        options_update(self)
        root = self.owner or root
        tick()
    end
end
