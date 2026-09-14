# IME Framework MVP Implementation

## Task
Add a TGS-native IME framework to the compositor. All input fields must activate TGS-native IME (not ibus/fcitx).

## Steps

1. [ ] Add `insert_widget_text` to `tgs_backend.h`
2. [ ] Create `src/compositor/ime.h`
3. [ ] Create `src/compositor/ime.c`
4. [ ] Modify `window_manager.c` — add type/window_id to widget_entry, IME activate/deactivate on focus/blur
5. [ ] Modify `event_engine.c` — route keys through IME
6. [ ] Modify `main.c` — initialize IME at startup
7. [ ] Compile check all modified/created files
