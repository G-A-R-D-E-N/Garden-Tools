# FO4 Record Editor Visual Overhaul

We have successfully combined the highly functional, data-dense structure of xEdit with the sleek, modern aesthetic of Visual Studio Code. This ensures the application remains powerful for mod conflict resolution while looking native and professional.

## 🎨 Global Aesthetics
- **Theme Switch:** The base styles in `App.xaml` were transitioned from generic gray WPF elements to the **VSCode Dark+** theme.
- **Accents:** System highlight colors and primary actions were updated to VSCode's signature blue (`#007ACC` / `#04395E`) for a modern feel.
- **Borders & Separators:** Replaced chunky, high-contrast borders with subtle `#2B2B2B` separator lines to create a clean layout.
- **Buttons:** Buttons now have a slightly flattened appearance and use smooth hover triggers matching standard VSCode secondary buttons.

## 🪟 Shell & Layout (`MainWindow.xaml`)
- **Activity Bar:** Positioned on the left, it now features a dark `#333333` background with white border highlights when an activity icon is active, mirroring VSCode's activity sidebar.
- **Grid Splitters:** Shrunk from thick 5-pixel bars to subtle 2-pixel lines, blending seamlessly into the interface while remaining easily grabbable.
- **Status Bar:** Changed to the vibrant VSCode blue (`#007ACC`) to clearly indicate application state and match industry-standard editor layouts.
- **Tabs:** Redesigned the `TabControl` templates. Active tabs seamlessly merge with the editor background (`#1E1E1E`) and feature an active top accent line (`#007ACC`).

## 📊 Data Grids & Views (`ConflictView.xaml`, `RecordEditorView.xaml`)
xEdit relies entirely on data grids to convey conflicts. To optimize this:
- **Grid Lines:** Enabled both horizontal and vertical grid lines for the main matrices, using a subtle `#2B2B2B` color so data boundaries are distinct (xEdit style) without visual noise.
- **Data Headers:** Styled to be crisp, semi-bold text with subtle padding and bottom borders, improving column scanability.
- **Background Tones:** Standardized the main data panels to `#1E1E1E` (identical to VSCode's main editor window) to ensure optimal text contrast.

## 🌈 Conflict Resolution Colors
xEdit's functional core is its color-coding. We enriched the existing color palette to look modern on a dark theme while keeping the exact semantic meaning modders expect.

| Status | Old Hex | New Hex | Visual Change |
|:---|:---|:---|:---|
| **Identical** (Green) | `#1F3A24` | `#234b23` | More vibrant, verdant green |
| **Override** (Yellow) | `#3D3A18` | `#5c5321` | Richer olive tone |
| **Winner** (Orange) | `#4A3618` | `#7b4b1a` | Clearer, burnt-orange |
| **Master** (Purple) | `#2E2340` | `#412a63` | Deeper, more distinct purple |
| **Loser** (Red) | `#3E2020` | `#662222` | Stronger, warning red |
| **Empty** | `#181818` | `#1e1e1e` | Matches main editor background |

### TreeView Integration
To match the grid colors, the plugin `TreeView` foreground colors (`ConflictStatusToBrush.cs`) were synchronized with VSCode symbol colors for high contrast:
- **Winner Text:** `#D7BA7D` (VSCode Light Brown)
- **Loser Text:** `#F44747` (VSCode Error Red)
- **Default Text:** `#CCCCCC`

> [!TIP]
> The updated palette vastly improves legibility over the older, muted tones, reducing eye strain during long conflict resolution sessions.
