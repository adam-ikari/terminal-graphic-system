# TGS Widget Reference

TGS defines 20 widget types (`tgs_widget_type` enum in `tgs_protocol.h`). The types and their
semantics are **protocol-neutral** — they name abstract UI concepts any rendering backend can
implement. The "Reference mapping" column records how the scene backend (SDL2 paint port) realizes each
type; swapping the backend only changes that column.

## Widget Kinds

| Widget | TGS Enum | Protocol semantics | Reference mapping (scene backend) |
|--------|----------|--------------------|----------------------------------|
| Button | `TGS_WIDGET_BUTTON` | Push button; delivers CLICK when activated | `lv_button` |
| Label | `TGS_WIDGET_LABEL` | Static text display; non-interactive | `lv_label` |
| Input | `TGS_WIDGET_INPUT` | Single-line text input; the IME-eligible type | `lv_textarea` |
| Checkbox | `TGS_WIDGET_CHECKBOX` | Toggle state; round variant (radio look) via RADIUS style | `lv_checkbox` |
| Slider | `TGS_WIDGET_SLIDER` | Value + range + drag capture; read-only variant = app ignores drag | `lv_slider` |
| Container | `TGS_WIDGET_CONTAINER` | Plain box; children at program-computed rects (layout is program policy) | plain scene node, no layout engine |
| Scroll | `TGS_WIDGET_SCROLL` | Scrollable viewport; scroll offset is mechanism | scrollable container |
| Image | `TGS_WIDGET_IMAGE` | Pixel buffer display; source via resources (stream 2) | `lv_image` |

## Derived Looks (style/attr compositions — not separate kinds)

| Look | Composition |
|------|-------------|
| Radio | CHECKBOX + `TGS_STYLE_RADIUS` (circle) on the indicator |
| Switch | CHECKBOX drawn wide with capsule styling |
| Progress | SLIDER whose drag the app ignores |
| List / Table | CONTAINER (or SCROLL) + child widgets at program-computed rects |
| Tab / Dropdown / Menu / pickers | need a popup layer — L4; until then app-side composition is blocked on that primitive |

Retired wire values (radio/switch/progress/list/table/menu/tab/dropdown/
timepick/datepick/vlayout/hlayout/glayout) are remapped by the compositor to
their primitive replacement for compatibility with older programs.
