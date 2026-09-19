# TGS Widget Reference

TGS defines 20 widget types (`tgs_widget_type` enum in `tgs_protocol.h`). The types and their
semantics are **protocol-neutral** — they name abstract UI concepts any rendering backend can
implement. The "Reference mapping" column records how the current LVGL backend realizes each
type; swapping the backend only changes that column.

## Basic Widgets

| Widget | TGS Enum | Protocol semantics | Reference mapping (LVGL backend) |
|--------|----------|--------------------|----------------------------------|
| Button | `TGS_WIDGET_BUTTON` | Push button; delivers CLICK when activated | `lv_button` |
| Label | `TGS_WIDGET_LABEL` | Static text display; non-interactive | `lv_label` |
| Input | `TGS_WIDGET_INPUT` | Single-line text input; the IME-eligible type | `lv_textarea` |
| Checkbox | `TGS_WIDGET_CHECKBOX` | Independent on/off toggle | `lv_checkbox` |
| Radio | `TGS_WIDGET_RADIO` | Visually round option marker (exclusivity is app policy) | round-styled `lv_checkbox` |

## Layout Containers

| Widget | TGS Enum | Protocol semantics | Reference mapping (LVGL backend) |
|--------|----------|--------------------|----------------------------------|
| VLayout | `TGS_WIDGET_VLAYOUT` | Vertical flex container — stacks children top to bottom | flex column container |
| HLayout | `TGS_WIDGET_HLAYOUT` | Horizontal flex container — stacks children left to right | flex row container |
| GLayout | `TGS_WIDGET_GLAYOUT` | Grid container — arranges children in rows and columns | grid container |
| Scroll | `TGS_WIDGET_SCROLL` | Scrollable viewport for overflow content | scrollable container |

## Complex Widgets

| Widget | TGS Enum | Protocol semantics | Reference mapping (LVGL backend) |
|--------|----------|--------------------|----------------------------------|
| List | `TGS_WIDGET_LIST` | `lv_list` | Scrollable list of items |
| Table | `TGS_WIDGET_TABLE` | `lv_table` | Row/column data grid |
| Menu | `TGS_WIDGET_MENU` | `lv_menu` | Hierarchical menu with submenus |
| Tab | `TGS_WIDGET_TAB` | `lv_tabview` | Tabbed container with switchable pages |
| Dropdown | `TGS_WIDGET_DROPDOWN` | `lv_dropdown` | Collapsible option selector |
| Image | `TGS_WIDGET_IMAGE` | `lv_image` | Image display (loaded via RESOURCE stream) |
| TimePick | `TGS_WIDGET_TIMEPICK` | `lv_roller` | Time picker widget (roller of time options) |
| DatePick | `TGS_WIDGET_DATEPICK` | `lv_calendar` | Date picker widget (calendar) |

## Creating Widgets

```c
/* API */
tgs_client_create_widget(type, id, parent_id, x, y, w, h, content);
```

- `type` — one of the `tgs_widget_type` values above
- `id` — unique integer ID within your app (you define these)
- `parent_id` — the window ID (for root widgets) or another widget ID (for nested layouts)
- `x, y, w, h` — position and size in terminal cells/pixels
- `content` — initial text content (button label, input text, etc.); can be empty string

## Styling Widgets

```c
tgs_client_set_widget_style(widget_id, prop, value);
```

Available style properties (`tgs_style_prop`):

| Property | Enum | Description |
|----------|------|-------------|
| Background color | `TGS_STYLE_BG_COLOR` | Widget background |
| Foreground color | `TGS_STYLE_FG_COLOR` | Text color |
| Corner radius | `TGS_STYLE_RADIUS` | Rounded corners |
| Border width | `TGS_STYLE_BORDER_WIDTH` | Border thickness |
| Border color | `TGS_STYLE_BORDER_COLOR` | Border color |
| Shadow width | `TGS_STYLE_SHADOW_WIDTH` | Drop shadow size |
| Shadow color | `TGS_STYLE_SHADOW_COLOR` | Drop shadow color |
| Font size | `TGS_STYLE_FONT_SIZE` | Text size |
| Opacity | `TGS_STYLE_OPACITY` | Widget opacity (0–255) |

## Layout Types

Containers support these layout modes (`tgs_layout_type`):

| Layout | Enum | Description |
|--------|------|-------------|
| None | `TGS_LAYOUT_NONE` | No automatic layout (manual x/y positioning) |
| Flex Row | `TGS_LAYOUT_FLEX_ROW` | Horizontal flex layout |
| Flex Column | `TGS_LAYOUT_FLEX_COL` | Vertical flex layout |
| Grid | `TGS_LAYOUT_GRID` | Grid layout |

```c
tgs_client_set_widget_style(widget_id, TGS_STYLE_RADIUS, 8);       /* rounded corners */
tgs_client_set_widget_layout(container_id, TGS_LAYOUT_FLEX_COL);    /* vertical stacking */
```

## Window Types

Windows are root containers. Each widget must belong to a window.

| Type | Enum | Description |
|------|------|-------------|
| Normal | `TGS_WINDOW_NORMAL` | Standard application window |
| Dialog | `TGS_WINDOW_DIALOG` | Modal dialog |
| Fullscreen | `TGS_WINDOW_FULLSCREEN` | Occupies entire terminal |
| Tool | `TGS_WINDOW_TOOL` | Hidden utility window (e.g. IME) |
